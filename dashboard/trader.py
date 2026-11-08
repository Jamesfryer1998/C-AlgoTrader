"""Starting and stopping the C++ trader, and running backtests, as ordinary processes.

The trader doesn't depend on the dashboard: it runs from the command line as before, keeps running if the
dashboard is closed, and the dashboard finds it again from the process id in its status.json.

    start      ./build/app/algo_trader_app, its output going to <records>/trader.log
    stop       SIGINT (as Ctrl+C): it finishes the minute, saves its records and exits
    flatten    SIGUSR1: it sends an order closing each position, then stops
"""

from __future__ import annotations

import os
import signal
import subprocess
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

from . import records
from .project import Project

TRADER_PROCESS_NAME = "algo_trader_app"
BACKTEST_TIMEOUT_SECONDS = 600
QUIET = "--quiet"
BACKTEST = "--backtest"


def is_trader_process(pid: int) -> bool:
    """Is `pid` alive and the trader (not some other program that has reused the number)?"""
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except (ProcessLookupError, PermissionError):
        return False
    try:
        command = subprocess.run(["ps", "-p", str(pid), "-o", "args="], capture_output=True, text=True,
                                 timeout=5).stdout
    except (OSError, subprocess.SubprocessError):
        return True   # can't tell: alive is the safer answer before sending it a signal
    return TRADER_PROCESS_NAME in command


@dataclass
class BacktestJob:
    day: str
    state: str = "running"     # running / done / failed
    output: str = ""
    started: float = field(default_factory=time.time)


class TraderControl:
    def __init__(self, project: Project):
        self.project = project
        self.jobs: dict[str, BacktestJob] = {}
        self.lock = threading.Lock()

    # ---------- Live trading ----------

    def running_pid(self) -> int | None:
        """The live trader's process id if it is running (whoever started it)."""
        pid = int(records.status(self.project.live_records).get("pid") or 0)
        state = records.status(self.project.live_records).get("state")
        return pid if state != "stopped" and is_trader_process(pid) else None

    def start(self) -> str:
        """Start live trading (paper, live or dry run, as config/general.json says). Returns what happened."""
        if self.running_pid():
            return "Already running"
        app = self.project.trader_app
        if not app.exists():
            return f"Not built yet: {app} is missing (run ./build_run.sh once)"
        folder = self.project.live_records
        folder.mkdir(parents=True, exist_ok=True)
        with (folder / records.LOG_FILE).open("a") as log:
            log.write(f"\n===== Started from the dashboard {time.strftime('%Y-%m-%d %H:%M:%S')} =====\n")
            log.flush()
            # Its own session: it keeps running if the dashboard is closed
            subprocess.Popen([str(app)], cwd=self.project.root, stdout=log, stderr=subprocess.STDOUT,
                             stdin=subprocess.DEVNULL, start_new_session=True)
        return "Starting"

    def stop(self) -> str:
        return self._signal(signal.SIGINT, "Stopping")

    def flatten(self) -> str:
        return self._signal(signal.SIGUSR1, "Closing every position, then stopping")

    def _signal(self, which: int, done: str) -> str:
        pid = self.running_pid()
        if not pid:
            return "Not running"
        os.kill(pid, which)
        return done

    # ---------- Backtests ----------

    def run_backtest(self, day: str) -> BacktestJob:
        """Backtest one day in the background (downloading its bars if they're missing)."""
        with self.lock:
            job = self.jobs.get(day)
            if job and job.state == "running":
                return job
            job = BacktestJob(day)
            self.jobs[day] = job
        threading.Thread(target=self._backtest, args=(job,), daemon=True).start()
        return job

    def _backtest(self, job: BacktestJob) -> None:
        try:
            done = subprocess.run([str(self.project.trader_app), BACKTEST, job.day, QUIET], cwd=self.project.root,
                                  capture_output=True, text=True, timeout=BACKTEST_TIMEOUT_SECONDS)
            job.output = (done.stdout + done.stderr).strip()
            job.state = "done" if done.returncode == 0 else "failed"
        except (OSError, subprocess.SubprocessError) as error:
            job.output = str(error)
            job.state = "failed"
