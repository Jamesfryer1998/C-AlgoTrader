"""The Research tab's studies, each a C++ app that backtests one idea and saves a report in its data folder:

    morning   morning_app   does trading each day's stocks in play make money?      config/morning.json
    pairs     pairs_app     do stocks that move together pay to trade against each other?   config/pairs.json
    swing     swing_app     do pullbacks, breakouts or the week's losers, held days to weeks, pay?   config/swing.json
    intraday  intraday_app  do stocks that stray from their sector in the day come back, hedged?   config/intraday.json
    timing    timing_app    of thousands of rules for when to hold SPY, does any time it well?   config/timing.json
    momentum  momentum_app  do published intraday momentum rules make money, before and since?   config/momentum.json
    rebalance rebalance_app does trading ahead of 60/40 funds' month-end rebalancing pay?   config/rebalance.json

    RUN RESEARCH      the app: downloads what it hasn't got, backtests the development and validation years and
                      saves data/<study>/report.json
    OPEN THE HOLDOUT  the app with --holdout: the final year too. Meant to be done once, when the rules are
                      settled; the first time is recorded (data/<study>/holdout_opened.json)
    CLEAR RESULTS     deletes every study's report, trades, progress and log. Downloaded bars and the holdout
                      records stay

See the research doc "Finding an Edge".
"""

from __future__ import annotations

import json
import os
import subprocess
import threading
import time
from dataclasses import dataclass
from datetime import timedelta
from pathlib import Path

from .project import Project

HOLDOUT = "--holdout"
PROGRESS_FILE = "progress.json"
REPORT_FILE = "report.json"
TRADES_FILE = "trades.csv"
RESULT_FILES = [REPORT_FILE, TRADES_FILE, PROGRESS_FILE, "rules.csv"]   # what CLEAR RESULTS deletes (with the log)
RUNNING, FINISHED, FAILED = "running", "finished", "failed"
STALE_AFTER = timedelta(minutes=30)       # a run whose progress hasn't moved for this long has died
RUN_TIMEOUT_SECONDS = 8 * 3600            # the first run downloads three years of data
NICENESS = 10                             # a lower CPU priority, so it never slows the trader


@dataclass(frozen=True)
class Study:
    key: str
    title: str
    app: str          # under the project root
    folder: str       # under data/
    log: str          # in its folder


MORNING = Study("morning", "Morning screen: stocks in play", "build/app/morning_app", "morning", "morning.log")
PAIRS = Study("pairs", "Pairs and lead-lag", "build/app/pairs_app", "pairs", "pairs.log")
SWING = Study("swing", "Swing trading", "build/app/swing_app", "swing", "swing.log")
INTRADAY = Study("intraday", "Intraday: stocks against their sector", "build/app/intraday_app", "intraday",
                 "intraday.log")
TIMING = Study("timing", "Market timing search: SPY", "build/app/timing_app", "timing", "timing.log")
MOMENTUM = Study("momentum", "Intraday momentum, as published", "build/app/momentum_app", "momentum", "momentum.log")
REBALANCE = Study("rebalance", "Month-end rebalancing", "build/app/rebalance_app", "rebalance", "rebalance.log")
STUDIES = {study.key: study for study in (MORNING, PAIRS, SWING, INTRADAY, TIMING, MOMENTUM, REBALANCE)}


@dataclass
class ResearchProgress:
    state: str = ""                       # running / finished / failed, or "" if never run
    stage: str = ""
    done: int = 0
    total: int = 0
    updated: float | None = None          # unix time
    seen_at: float | None = None          # when the dashboard first saw this stage, and how far it had got
    seen_done: int = 0

    @property
    def fraction(self) -> float:
        return self.done / self.total if self.total else 0.0

    def seconds_left(self, now: float) -> float | None:
        """From this stage's pace since the dashboard first saw it; None until it has moved."""
        if self.state != RUNNING or self.seen_at is None or self.done <= self.seen_done or now <= self.seen_at:
            return None
        per_item = (now - self.seen_at) / (self.done - self.seen_done)
        return per_item * (self.total - self.done)


def study_of(key: str | None) -> Study:
    return STUDIES.get(key or "", MORNING)


def folder(project: Project, study: Study = MORNING) -> Path:
    return project.data_root / study.folder


def load_report(project: Project, study: Study = MORNING) -> dict | None:
    """A study's last report.json, or None if there isn't one yet."""
    try:
        return json.loads((folder(project, study) / REPORT_FILE).read_text())
    except (OSError, ValueError):
        return None


def clear_results(project: Project, controls: dict[str, "ResearchControl"]) -> str:
    """Delete every study's report, trades, progress and log, unless one is running. Returns what happened."""
    if any(control.running() for control in controls.values()):
        return "Not while research is running"
    removed = 0
    for study in STUDIES.values():
        for name in RESULT_FILES + [study.log]:
            path = folder(project, study) / name
            if path.exists():
                path.unlink()
                removed += 1
    return f"Cleared {removed} result files" if removed else "Nothing to clear"


class ResearchControl:
    """Runs one study's app in the background, one run at a time."""

    def __init__(self, project: Project, study: Study = MORNING):
        self.project = project
        self.study = study
        self.lock = threading.Lock()
        self.process_running = False
        self.last_error = ""
        self.stage_seen: dict[str, tuple[float, int]] = {}   # stage -> (first seen, done then)

    def progress(self, now: float | None = None) -> ResearchProgress:
        """From progress.json, so a run started before the dashboard restarted still shows."""
        now = now or time.time()
        try:
            saved = json.loads((folder(self.project, self.study) / PROGRESS_FILE).read_text())
        except (OSError, ValueError):
            return ResearchProgress(state=FAILED if self.last_error else "")
        progress = ResearchProgress(state=saved.get("state", ""), stage=saved.get("stage", ""),
                                    done=int(saved.get("done", 0)), total=int(saved.get("total", 0)),
                                    updated=saved.get("updated"))
        progress.seen_at, progress.seen_done = self.stage_seen.setdefault(progress.stage, (now, progress.done))
        stale = progress.updated is not None and now - progress.updated > STALE_AFTER.total_seconds()
        if progress.state == RUNNING and stale and not self.process_running:
            progress.state = FAILED
        if self.last_error and not self.process_running:
            progress.state = FAILED
        return progress

    def running(self) -> bool:
        return self.process_running or self.progress().state == RUNNING

    def run(self, holdout: bool = False) -> str:
        """Start a run now. Returns what happened."""
        with self.lock:
            if self.running():
                return "Research is already running"
            app = self.project.root / self.study.app
            if not app.exists():
                return f"Not built yet: {app} is missing"
            self.process_running = True
            self.last_error = ""
            self.stage_seen = {}
        threading.Thread(target=self._run, args=(app, holdout), daemon=True).start()
        return "Opening the holdout" if holdout else "Research started"

    def _run(self, app: Path, holdout: bool) -> None:
        folder(self.project, self.study).mkdir(parents=True, exist_ok=True)
        log = folder(self.project, self.study) / self.study.log
        command = [str(app)] + ([HOLDOUT] if holdout else [])
        try:
            with log.open("w") as output:
                done = subprocess.run(command, cwd=self.project.root, stdout=output, stderr=subprocess.STDOUT,
                                      timeout=RUN_TIMEOUT_SECONDS, preexec_fn=_lower_priority)
            if done.returncode != 0:
                self.last_error = f"{Path(self.study.app).name} stopped with code {done.returncode}: see {log}"
        except (OSError, subprocess.SubprocessError) as error:
            self.last_error = str(error)
        finally:
            self.process_running = False


def _lower_priority() -> None:
    os.nice(NICENESS)
