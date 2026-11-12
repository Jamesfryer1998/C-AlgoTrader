"""Backtest each day the live account traded, automatically, once that day is over.

The backtest starts as the live account did that day (its equity at the first minute and the positions it
carried in, as the Backtest vs live tab's RUN BACKTEST does), so the two can be compared the next morning.

Each traded day's week's picks (picks.py) are backtested on it too, and at the weekend the weekly screen that
makes them is started.

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

from pathlib import Path

from . import comparison, metrics, picks, records
from .project import Project
from .trader import BacktestStart, TraderControl

BARS_READY_AFTER_CLOSE = timedelta(minutes=20)   # Alpaca's free data is 16 minutes behind
CHECK_EVERY_SECONDS = 60
RETRY_AFTER = timedelta(hours=1)
RUNNING = "running"
FAILED = "failed"


def bars_ready_at(day: str) -> datetime:
    """When all of `day`'s bars can be downloaded: its 16:00 New York close plus the free data's delay, in UTC."""
    close = datetime.combine(datetime.fromisoformat(day).date(), metrics.MARKET_CLOSE, tzinfo=metrics.NEW_YORK)
    return (close + BARS_READY_AFTER_CLOSE).astimezone(timezone.utc)


def made_at(folder: Path) -> datetime | None:
    """When a backtest's records folder last wrote its equity curve, if it has one."""
    equity = folder / records.EQUITY_FILE
    if not equity.exists():
        return None
    return datetime.fromtimestamp(equity.stat().st_mtime, tz=timezone.utc)


def _needs_backtest(folder: Path, day: str, now: datetime) -> bool:
    """Is the day over with all its bars in, and has this backtest not been made since?"""
    ready = bars_ready_at(day)
    made = made_at(folder)
    return now >= ready and (made is None or made < ready)


def due_days(project: Project, now: datetime) -> list[str]:
    """Days the live account traded, whose bars are all in, without a backtest made since then. Oldest first."""
    return [day for day in sorted(comparison.traded_days(project.live_records))
            if _needs_backtest(project.backtest_records(day), day, now)]


def due_picks_days(project: Project, now: datetime) -> list[str]:
    """Traded days with weekly picks from before them, whose picks haven't been backtested since. Oldest first."""
    return [day for day in sorted(comparison.traded_days(project.live_records))
            if picks.picks_for_day(project, day) and _needs_backtest(picks.backtest_folder(project, day), day, now)]


def picks_job(day: str) -> str:
    """What a day's picks backtest is called among the dashboard's jobs (the live one is just the day)."""
    return f"picks {day}"


class EndOfDayBacktests:
    """Once a minute: start the next due backtest through the dashboard's TraderControl (so the tabs show it
    running), one at a time: each traded day as live started it, then that week's picks on it. At the weekend it
    also starts the weekly screen, if this weekend's picks aren't saved yet."""

    def __init__(self, project: Project, control: TraderControl, screen: picks.ScreenControl | None = None):
        self.project = project
        self.control = control
        self.screen = screen
        self.failed_at: dict[str, datetime] = {}

    def check(self, now: datetime) -> str | None:
        """Start the next due job, if none is running. Returns its name."""
        if self.screen and not self.screen.running() and picks.weekend_screen_due(self.project, now):
            print("Weekly screen for next week's picks", flush=True)
            self.screen.run()
            return "weekly screen"
        if any(job.state == RUNNING for job in self.control.jobs.values()):
            return None
        for name, job in self.control.jobs.items():
            if job.state == FAILED and name not in self.failed_at:
                self.failed_at[name] = now
                print(f"End-of-day backtest {name} failed; trying again in an hour:\n{job.output[-500:]}", flush=True)

        live = comparison.live_start
        due = [(day, day, lambda day: live(self.project.live_records, day)) for day in due_days(self.project, now)]
        due += [(picks_job(day), day, self._picks_start) for day in due_picks_days(self.project, now)]
        for name, day, start_of in due:
            failed = self.failed_at.get(name)
            if failed and now - failed < RETRY_AFTER:
                continue
            self.failed_at.pop(name, None)
            start = start_of(day)
            print(f"End-of-day backtest {name} {start.describe()}", flush=True)
            self.control.run_backtest(day, start, name)
            return name
        return None

    def _picks_start(self, day: str) -> BacktestStart:
        """The week's picks on `day`, from the live account's money that morning, flat (they start fresh)."""
        run = picks.picks_for_day(self.project, day)
        return BacktestStart(capital=comparison.live_start(self.project.live_records, day).capital,
                             portfolio=picks.portfolio_file(self.project, run),
                             records=f"{picks.PICKS_BACKTESTS}/{day}")

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
