import math

import pandas as pd
import pytest

from dashboard import records, report


def equity_of(closes: dict[str, list[float]]) -> pd.DataFrame:
    """An equity curve: {day: [first minute, ..., close]}."""
    rows = [{"time": f"{day} 09:{30 + i:02d}:00", "equity": value, "day": day}
            for day, values in closes.items() for i, value in enumerate(values)]
    return pd.DataFrame(rows)


THREE_DAYS = equity_of({"2026-10-29": [100000, 101000],      # +1%
                        "2026-10-30": [101000, 98980],       # -2% on the 101,000 close
                        "2026-11-02": [98980, 100959.6]})    # +2%


def test_each_day_against_the_previous_close():
    days = report.daily(THREE_DAYS)
    assert list(days["day"]) == ["2026-10-29", "2026-10-30", "2026-11-02"]
    assert [round(r, 4) for r in days["return"]] == [0.01, -0.02, 0.02]
    assert round(days["growth"].iloc[-1], 6) == 0.009596              # 100,959.60 against 100,000
    assert [round(d, 4) for d in days["drawdown"]] == [0.0, -0.02, -0.0004]   # below the 101,000 high


def test_months_compound_their_days():
    months = report.monthly_returns(report.daily(THREE_DAYS))
    assert list(months.columns) == list(range(1, 13)) and list(months.index) == [2026]
    assert round(months.loc[2026, 10], 4) == round(1.01 * 0.98 - 1, 4)   # October: +1% then -2%
    assert round(months.loc[2026, 11], 4) == 0.02
    assert math.isnan(months.loc[2026, 1])                               # not traded


def test_summary():
    short = report.summary(report.daily(THREE_DAYS))
    assert short.days == 3 and round(short.total_return, 6) == 0.009596
    assert round(short.max_drawdown, 4) == -0.02 and round(short.best_day, 4) == 0.02
    assert round(short.winning_days, 4) == round(2 / 3, 4)
    assert short.annualised_return is None and short.sharpe is None     # 3 days: too few to annualise

    steady = report.daily(equity_of({f"2026-10-{d:02d}": [100 + d, 101 + d] for d in range(1, 26)}))
    assert report.summary(steady).sharpe > 0 and report.summary(steady).annualised_return is not None
    assert report.summary(report.daily(equity_of({}))).days == 0


def test_rolling_and_quantiles():
    days = report.daily(equity_of({f"2026-10-{d:02d}": [100.0, 100.0 + d % 3] for d in range(1, 26)}))
    window = report.rolling(days, 5)
    assert len(window) == len(days) - 4                      # from the 5th day on
    assert (window["volatility"] > 0).all()
    quantiles = report.normal_quantiles(4)
    assert quantiles[0] == pytest.approx(-quantiles[-1]) and quantiles[0] < quantiles[1] < 0


def test_sources_are_live_then_backtests_newest_first(project):
    for day in ["2026-10-01", "2026-11-09"]:
        folder = project.backtest_records(day)
        folder.mkdir(parents=True)
        (folder / records.EQUITY_FILE).write_text("time,equity\n")
    project.backtest_records("2026-10-15").mkdir(parents=True)          # no equity curve: not offered
    assert [value for value, _ in report.sources(project)] == [report.LIVE_SOURCE, "2026-11-09", "2026-10-01"]
    assert report.folder_of(project, "2026-11-09") == project.backtest_records("2026-11-09")
    assert report.folder_of(project, report.LIVE_SOURCE) == project.live_records
