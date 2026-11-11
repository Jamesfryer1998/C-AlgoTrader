import os
from datetime import datetime, timezone

from dashboard import end_of_day, records
from dashboard.trader import BacktestJob
from dashboard.tests.test_records_and_metrics import write_trades

DAY = "2026-11-10"                                                    # New York closes 21:00 UTC (winter)
AFTER_THE_CLOSE = datetime(2026, 11, 10, 21, 25, tzinfo=timezone.utc)   # bars all in from 21:20
DURING_THE_DAY = datetime(2026, 11, 10, 18, 0, tzinfo=timezone.utc)


class FakeControl:
    """TraderControl's backtest side, recording what was started instead of running the trader."""

    def __init__(self):
        self.jobs: dict[str, BacktestJob] = {}
        self.started = []

    def run_backtest(self, day, start):
        self.started.append((day, start))
        self.jobs[day] = BacktestJob(day, start)
        return self.jobs[day]


def traded(project, *days):
    write_trades(project.live_records, [(f"{day} 10:00:00", "KO", "buy", 1, 60, "sent") for day in days])
    (project.live_records / records.EQUITY_FILE).write_text(
        "time,equity\n" + "".join(f"{day} 09:30:00,99000\n" for day in days))


def backtested(project, day, at: datetime):
    folder = project.backtest_records(day)
    folder.mkdir(parents=True, exist_ok=True)
    (folder / records.EQUITY_FILE).write_text("time,equity\n")
    os.utime(folder / records.EQUITY_FILE, (at.timestamp(), at.timestamp()))


def test_bars_are_ready_twenty_minutes_after_the_new_york_close():
    assert end_of_day.bars_ready_at(DAY) == datetime(2026, 11, 10, 21, 20, tzinfo=timezone.utc)
    assert end_of_day.bars_ready_at("2026-07-10") == datetime(2026, 7, 10, 20, 20, tzinfo=timezone.utc)   # summer


def test_a_day_is_due_once_over_unless_backtested_since(project):
    traded(project, "2026-11-09", DAY)
    assert end_of_day.due_days(project, DURING_THE_DAY) == ["2026-11-09"]          # today isn't over
    assert end_of_day.due_days(project, AFTER_THE_CLOSE) == ["2026-11-09", DAY]    # oldest first

    backtested(project, "2026-11-09", datetime(2026, 11, 9, 22, 0, tzinfo=timezone.utc))   # after its close
    backtested(project, DAY, DURING_THE_DAY)                                               # stopped mid-day
    assert end_of_day.due_days(project, AFTER_THE_CLOSE) == [DAY]


def test_starts_one_backtest_at_a_time_as_live_started_the_day(project):
    traded(project, "2026-11-09", DAY)
    control = FakeControl()
    backtests = end_of_day.EndOfDayBacktests(project, control)

    assert backtests.check(AFTER_THE_CLOSE) == "2026-11-09"
    day, start = control.started[0]
    assert start.capital == 99000 and start.positions == []         # the live account's start that day
    assert backtests.check(AFTER_THE_CLOSE) is None                 # the first is still running

    control.jobs["2026-11-09"].state = "done"
    backtested(project, "2026-11-09", AFTER_THE_CLOSE)
    assert backtests.check(AFTER_THE_CLOSE) == DAY
    assert [day for day, _ in control.started] == ["2026-11-09", DAY]


def test_a_failed_backtest_is_tried_again_an_hour_later(project):
    traded(project, DAY)
    control = FakeControl()
    backtests = end_of_day.EndOfDayBacktests(project, control)
    backtests.check(AFTER_THE_CLOSE)
    control.jobs[DAY].state = "failed"

    assert backtests.check(AFTER_THE_CLOSE) is None                 # not straight away
    later = AFTER_THE_CLOSE + end_of_day.RETRY_AFTER
    assert backtests.check(later) == DAY
