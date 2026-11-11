"""Backtest each day the live account traded, automatically, once that day is over.

The backtest starts as the live account did that day (its equity at the first minute and the positions it
carried in, as the Backtest vs live tab's RUN BACKTEST does), so the two can be compared the next morning.

It runs inside the dashboard (on the home server, the dashboard's service is always up), checking once a
minute. A day is due once its bars are all in at Alpaca (the free data is 16 minutes behind the close) and it
has no backtest made since then: one run during the day stopped where the day was, so it's run again.
One backtest at a time; a failed one is tried again an hour later. The trader itself is never touched.
"""

from __future__ import annotations

import threading
import time
import traceback
from datetime import datetime, timedelta, timezone

from . import comparison, metrics, records
from .project import Project
from .trader import TraderControl

BARS_READY_AFTER_CLOSE = timedelta(minutes=20)   # Alpaca's free data is 16 minutes behind
CHECK_EVERY_SECONDS = 60
RETRY_AFTER = timedelta(hours=1)
RUNNING = "running"
FAILED = "failed"


def bars_ready_at(day: str) -> datetime:
    """When all of `day`'s bars can be downloaded: its 16:00 New York close plus the free data's delay, in UTC."""
    close = datetime.combine(datetime.fromisoformat(day).date(), metrics.MARKET_CLOSE, tzinfo=metrics.NEW_YORK)
    return (close + BARS_READY_AFTER_CLOSE).astimezone(timezone.utc)


def made_at(project: Project, day: str) -> datetime | None:
    """When `day`'s backtest last wrote its equity curve, if it has one."""
    equity = project.backtest_records(day) / records.EQUITY_FILE
    if not equity.exists():
        return None
    return datetime.fromtimestamp(equity.stat().st_mtime, tz=timezone.utc)


def due_days(project: Project, now: datetime) -> list[str]:
    """Days the live account traded, whose bars are all in, without a backtest made since then. Oldest first."""
    due = []
    for day in sorted(comparison.traded_days(project.live_records)):
        ready = bars_ready_at(day)
        made = made_at(project, day)
        if now >= ready and (made is None or made < ready):
            due.append(day)
    return due


class EndOfDayBacktests:
    """Starts the next due day's backtest through the dashboard's TraderControl (so the Backtest vs live tab
    shows it running), one at a time."""

    def __init__(self, project: Project, control: TraderControl):
        self.project = project
        self.control = control
        self.failed_at: dict[str, datetime] = {}

    def check(self, now: datetime) -> str | None:
        """Start the next due backtest, if none is running. Returns the day started."""
        if any(job.state == RUNNING for job in self.control.jobs.values()):
            return None
        for day, job in self.control.jobs.items():
            if job.state == FAILED and day not in self.failed_at:
                self.failed_at[day] = now
                print(f"End-of-day backtest of {day} failed; trying again in an hour:\n{job.output[-500:]}", flush=True)
        for day in due_days(self.project, now):
            failed = self.failed_at.get(day)
            if failed and now - failed < RETRY_AFTER:
                continue
            self.failed_at.pop(day, None)
            start = comparison.live_start(self.project.live_records, day)
            print(f"End-of-day backtest of {day} {start.describe()}", flush=True)
            self.control.run_backtest(day, start)
            return day
        return None

    def start(self) -> None:
        """Check once a minute, in the background, for as long as the dashboard runs."""
        threading.Thread(target=self._run, name="end-of-day-backtests", daemon=True).start()

    def _run(self) -> None:
        while True:
            # Wait first: a process that only imports the dashboard (the deploy's smoke test) is gone by then
            time.sleep(CHECK_EVERY_SECONDS)
            try:
                self.check(datetime.now(timezone.utc))
            except Exception:   # keep checking: one bad day's records mustn't stop the next days' backtests
                traceback.print_exc()
