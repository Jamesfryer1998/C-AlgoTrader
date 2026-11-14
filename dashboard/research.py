"""The morning screen (morning_app): does trading each day's stocks in play make money?

    RUN RESEARCH      morning_app: downloads what it hasn't got (the first run takes a while), then backtests the
                      development and validation years and saves data/morning/report.json
    OPEN THE HOLDOUT  morning_app --holdout: the final year too. Meant to be done once, when the rules are settled;
                      the first time is recorded (data/morning/holdout_opened.json)

See the research doc "Finding an Edge" and config/morning.json for the rules.
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

MORNING_APP = "build/app/morning_app"
HOLDOUT = "--holdout"
MORNING_FOLDER = "morning"                # under data/
PROGRESS_FILE = "progress.json"
REPORT_FILE = "report.json"
LOG_FILE = "morning.log"
RUNNING, FINISHED, FAILED = "running", "finished", "failed"
STALE_AFTER = timedelta(minutes=30)       # a run whose progress hasn't moved for this long has died
RUN_TIMEOUT_SECONDS = 8 * 3600            # the first run downloads three years of data
NICENESS = 10                             # a lower CPU priority, so it never slows the trader


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


def folder(project: Project) -> Path:
    return project.data_root / MORNING_FOLDER


def load_report(project: Project) -> dict | None:
    """The last run's report.json, or None if there isn't one yet."""
    try:
        return json.loads((folder(project) / REPORT_FILE).read_text())
    except (OSError, ValueError):
        return None


class ResearchControl:
    """Runs morning_app in the background, one at a time."""

    def __init__(self, project: Project):
        self.project = project
        self.lock = threading.Lock()
        self.process_running = False
        self.last_error = ""
        self.stage_seen: dict[str, tuple[float, int]] = {}   # stage -> (first seen, done then)

    def progress(self, now: float | None = None) -> ResearchProgress:
        """From progress.json, so a run started before the dashboard restarted still shows."""
        now = now or time.time()
        try:
            saved = json.loads((folder(self.project) / PROGRESS_FILE).read_text())
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
            app = self.project.root / MORNING_APP
            if not app.exists():
                return f"Not built yet: {app} is missing"
            self.process_running = True
            self.last_error = ""
            self.stage_seen = {}
        threading.Thread(target=self._run, args=(app, holdout), daemon=True).start()
        return "Opening the holdout" if holdout else "Research started"

    def _run(self, app: Path, holdout: bool) -> None:
        folder(self.project).mkdir(parents=True, exist_ok=True)
        log = folder(self.project) / LOG_FILE
        command = [str(app)] + ([HOLDOUT] if holdout else [])
        try:
            with log.open("w") as output:
                done = subprocess.run(command, cwd=self.project.root, stdout=output, stderr=subprocess.STDOUT,
                                      timeout=RUN_TIMEOUT_SECONDS, preexec_fn=_lower_priority)
            if done.returncode != 0:
                self.last_error = f"morning_app stopped with code {done.returncode}: see {log}"
        except (OSError, subprocess.SubprocessError) as error:
            self.last_error = str(error)
        finally:
            self.process_running = False


def _lower_priority() -> None:
    os.nice(NICENESS)
