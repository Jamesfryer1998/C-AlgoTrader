"""The weekly picks: a rolling screen suggests a portfolio, and each traded day it's backtested against live.

    every weekend (or the Picks tab's button)   screen_app --weekly: tune every strategy on every stock on the
                                                last months, keep what held up on the final month it never saw.
                                                Its top picks are saved as data/picks/<date>/
    after each traded day's close               that week's picks are backtested on the day, starting with the
                                                live account's money (data/picks/backtest/<day>/), next to live
                                                and live's own portfolio backtested (end_of_day.py)

Nothing here changes config/strategy.json: the picks are a suggestion to compare, until you choose to use them.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path

import pandas as pd

from . import records
from .project import Project

SCREEN_APP = "build/app/screen_app"
WEEKLY = "--weekly"
TOP = "--top"
THREADS = "--threads"
TOP_PICKS = 5
SCREEN_RESULTS = "screen/weekly"          # under data/: where screen_app --weekly writes
PICKS_FOLDER = "picks"                   # under data/: data/picks/<date>/ a saved run, data/picks/backtest/<day>/
PICKS_BACKTESTS = "picks/backtest"
PROGRESS_FILE = "progress.json"
LOG_FILE = "screen.log"
PICKS_CSV = "top_picks.csv"
PICKS_PORTFOLIO = "top_picks_config.json"
RESULTS_CSV = "screen_results.csv"
SAVED_FILES = [PICKS_CSV, PICKS_PORTFOLIO, RESULTS_CSV, LOG_FILE]
SCREENING, FINISHED, FAILED = "screening", "finished", "failed"
STALE_AFTER = timedelta(minutes=30)      # a screen that hasn't moved for this long has died
SCREEN_TIMEOUT_SECONDS = 6 * 3600
WEEKEND_FROM = (5, 6)                    # the weekly screen runs from Saturday 06:00 UTC (Saturday = weekday 5)


# ---------- Saved runs ----------

def picks_root(project: Project) -> Path:
    return project.data_root / PICKS_FOLDER


def saved_runs(project: Project) -> list[str]:
    """The dates of the saved weekly picks, newest first."""
    root = picks_root(project)
    if not root.is_dir():
        return []
    return sorted((run.name for run in root.iterdir() if (run / PICKS_PORTFOLIO).exists()), reverse=True)


def picks_for_day(project: Project, day: str) -> str | None:
    """The picks a day would have traded: the newest run saved before that day."""
    return next((run for run in saved_runs(project) if run < day), None)


def portfolio_file(project: Project, run: str) -> Path:
    return picks_root(project) / run / PICKS_PORTFOLIO


def picks_table(project: Project, run: str) -> pd.DataFrame:
    """A run's top picks and why: its training and test Sharpe, buy and hold's, the test trades."""
    path = picks_root(project) / run / PICKS_CSV
    try:
        frame = pd.read_csv(path)
    except (OSError, ValueError, pd.errors.EmptyDataError):
        return pd.DataFrame()
    frame["parameters"] = frame["parameters"].map(_settings_text)
    return frame


def _settings_text(parameters: str) -> str:
    """{"period": 14, "stop_loss": 2} -> "period 14, stop_loss 2"."""
    try:
        return ", ".join(f"{name} {value:g}" if isinstance(value, (int, float)) else f"{name} {value}"
                         for name, value in json.loads(parameters).items())
    except (TypeError, ValueError):
        return str(parameters)


def backtest_folder(project: Project, day: str) -> Path:
    return project.data_root / PICKS_BACKTESTS / day


# ---------- Running the screen ----------

@dataclass
class ScreenProgress:
    state: str                       # screening / finished / failed, or "" if never run
    done: int = 0
    total: int = 0
    stock: str = ""
    started: float | None = None     # unix time
    updated: float | None = None

    @property
    def fraction(self) -> float:
        return self.done / self.total if self.total else 0.0

    def seconds_left(self, now: float) -> float | None:
        """From the time per stock so far (downloads included). None until the first stock is done."""
        if self.state != SCREENING or not self.done or self.started is None:
            return None
        per_stock = (now - self.started) / self.done
        return per_stock * (self.total - self.done)


class ScreenControl:
    """Runs screen_app --weekly in the background (one at a time) and saves its picks when it finishes."""

    def __init__(self, project: Project):
        self.project = project
        self.lock = threading.Lock()
        self.process_running = False
        self.last_error = ""

    @property
    def results(self) -> Path:
        return self.project.data_root / SCREEN_RESULTS

    def progress(self, now: float | None = None) -> ScreenProgress:
        """How far the screen has got, from its progress.json (so a screen started before the dashboard
        restarted still shows). A screen that stopped moving long ago, outside this dashboard, has died."""
        now = now or time.time()
        try:
            saved = json.loads((self.results / PROGRESS_FILE).read_text())
        except (OSError, ValueError):
            return ScreenProgress(state=FAILED if self.last_error else "")
        progress = ScreenProgress(state=saved.get("state", ""), done=int(saved.get("done", 0)),
                                  total=int(saved.get("total", 0)), stock=saved.get("stock", ""),
                                  started=saved.get("started"), updated=saved.get("updated"))
        stale = progress.updated is not None and now - progress.updated > STALE_AFTER.total_seconds()
        if progress.state == SCREENING and stale and not self.process_running:
            progress.state = FAILED
        if self.last_error and not self.process_running:
            progress.state = FAILED
        return progress

    def running(self) -> bool:
        return self.process_running or self.progress().state == SCREENING

    def run(self) -> str:
        """Start a screen now. Returns what happened."""
        with self.lock:
            if self.running():
                return "A screen is already running"
            app = self.project.root / SCREEN_APP
            if not app.exists():
                return f"Not built yet: {app} is missing"
            self.process_running = True
            self.last_error = ""
        threading.Thread(target=self._screen, args=(app,), daemon=True).start()
        return "Screening started"

    def _screen(self, app: Path) -> None:
        self.results.mkdir(parents=True, exist_ok=True)
        log = self.results / LOG_FILE
        try:
            with log.open("w") as output:
                done = subprocess.run([str(app), WEEKLY, TOP, str(TOP_PICKS), THREADS, str(os.cpu_count() or 1)],
                                      cwd=self.project.root, stdout=output, stderr=subprocess.STDOUT,
                                      timeout=SCREEN_TIMEOUT_SECONDS)
            if done.returncode != 0 or not (self.results / PICKS_PORTFOLIO).exists():
                self.last_error = f"screen_app stopped with code {done.returncode}: see {log}"
            else:
                self.save_run(datetime.now(timezone.utc).strftime("%Y-%m-%d"))
        except (OSError, subprocess.SubprocessError) as error:
            self.last_error = str(error)
        finally:
            self.process_running = False

    def save_run(self, date: str) -> Path:
        """Keep this screen's picks, results and output as data/picks/<date>/."""
        folder = picks_root(self.project) / date
        folder.mkdir(parents=True, exist_ok=True)
        for name in SAVED_FILES:
            if (self.results / name).exists():
                shutil.copy2(self.results / name, folder / name)
        return folder


def weekend_screen_due(project: Project, now: datetime) -> bool:
    """From Saturday 06:00 UTC to the end of Sunday, if this weekend's picks haven't been saved yet."""
    if now.weekday() not in WEEKEND_FROM:
        return False
    saturday = (now - timedelta(days=now.weekday() - WEEKEND_FROM[0])).replace(hour=6, minute=0, second=0,
                                                                                  microsecond=0)
    if now < saturday:
        return False
    runs = saved_runs(project)
    return not runs or runs[0] < saturday.strftime("%Y-%m-%d")


# ---------- Picks against live, day by day ----------

def day_pnl(folder: Path, day: str) -> float | None:
    """A day's P&L in a records folder: its last equity against its first."""
    equity = records.equity(folder)
    that_day = equity[equity["day"] == day]["equity"]
    return float(that_day.iloc[-1] - that_day.iloc[0]) if len(that_day) else None


def comparison(project: Project, days: list[str]) -> pd.DataFrame:
    """For each day: live's P&L, live's portfolio backtested, the picks backtested, and which picks. Oldest first."""
    rows = [{"day": day,
             "live": day_pnl(project.live_records, day),
             "live_backtest": day_pnl(project.backtest_records(day), day),
             "picks": day_pnl(backtest_folder(project, day), day),
             "picks_run": picks_for_day(project, day)}
            for day in sorted(days)]
    frame = pd.DataFrame(rows, columns=["day", "live", "live_backtest", "picks", "picks_run"])
    frame["picks_minus_live"] = frame["picks"] - frame["live"]
    return frame
