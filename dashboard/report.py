"""A performance report of an account's equity curve, day by day: the numbers behind the Report tab.

Works on any records folder with an equity.csv: the live account, or a backtest (a multi-day one, e.g.
algo_trader_app --backtest 2026-10-01 --days 31, says the most). Returns are fractions (0.012 = 1.2%).
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from statistics import NormalDist

import pandas as pd

from . import records

TRADING_DAYS_A_YEAR = 252
ROLLING_DAYS = 20           # the rolling return and volatility look back this many trading days
MIN_DAYS_FOR_SHAPE = 5      # the distribution, Q-Q and rolling charts need a few days to say anything
MIN_DAYS_TO_ANNUALISE = 20  # fewer, and a year's worth of them is a wild guess (one +1% day is +1,100% a year)
LIVE_SOURCE = "live"        # the report's source picker: "live", or a backtest's folder name (its first day)
BACKTEST_FOLDER = "backtest"


# ---------- Day by day ----------

def daily(equity: pd.DataFrame) -> pd.DataFrame:
    """One row per day: its first and last equity, its return, the growth since the start and the drawdown.

    A day's return is its close against the previous close; the first day's is against its own first minute
    (there's no close before it).
    """
    if equity.empty:
        return pd.DataFrame(columns=["day", "open", "close", "return", "growth", "drawdown"])
    days = equity.groupby("day", sort=True)["equity"].agg(open="first", close="last").reset_index()
    previous_close = days["close"].shift(1).fillna(days["open"])
    days["return"] = days["close"] / previous_close - 1
    days["growth"] = days["close"] / days["open"].iloc[0] - 1                 # since the first minute recorded
    peak = days["close"].cummax().clip(lower=days["open"].iloc[0])
    days["drawdown"] = days["close"] / peak - 1                               # 0 at a new high, negative below it
    return days


def monthly_returns(days: pd.DataFrame) -> pd.DataFrame:
    """Each month's compounded return: a row per year, a column per month (1-12), NaN for months not traded."""
    if days.empty:
        return pd.DataFrame()
    dates = pd.to_datetime(days["day"])
    grouped = (1 + days["return"]).groupby([dates.dt.year, dates.dt.month]).prod() - 1
    table = grouped.unstack()
    return table.reindex(columns=range(1, 13))


def rolling(days: pd.DataFrame, window: int = ROLLING_DAYS) -> pd.DataFrame:
    """The return over the last `window` days and their volatility (annualised), day by day."""
    growth = (1 + days["return"]).rolling(window).apply(lambda returns: returns.prod(), raw=True) - 1
    volatility = days["return"].rolling(window).std() * TRADING_DAYS_A_YEAR ** 0.5
    return pd.DataFrame({"day": days["day"], "return": growth, "volatility": volatility}).dropna()


def normal_quantiles(count: int) -> list[float]:
    """Where `count` sorted values from a normal distribution would sit, for the Q-Q plot."""
    normal = NormalDist()
    return [normal.inv_cdf((i + 0.5) / count) for i in range(count)]


# ---------- Headline numbers ----------

@dataclass
class Summary:
    days: int
    total_return: float | None
    annualised_return: float | None
    volatility: float | None          # annualised
    sharpe: float | None              # annualised, against no risk-free rate
    sortino: float | None
    max_drawdown: float | None        # negative
    best_day: float | None
    worst_day: float | None
    winning_days: float | None        # the share of days that made money


def summary(days: pd.DataFrame) -> Summary:
    count = len(days)
    if not count:
        return Summary(0, *[None] * 9)
    returns = days["return"]
    total = float(days["growth"].iloc[-1])
    annualise = count >= MIN_DAYS_TO_ANNUALISE
    deviation = returns.std() if annualise else None
    downside = returns[returns < 0].std() if annualise and (returns < 0).sum() > 1 else None
    yearly = TRADING_DAYS_A_YEAR ** 0.5
    return Summary(
        days=count,
        total_return=total,
        annualised_return=(1 + total) ** (TRADING_DAYS_A_YEAR / count) - 1 if annualise else None,
        volatility=deviation * yearly if deviation else None,
        sharpe=returns.mean() / deviation * yearly if deviation else None,
        sortino=returns.mean() / downside * yearly if downside else None,
        max_drawdown=float(days["drawdown"].min()),
        best_day=float(returns.max()),
        worst_day=float(returns.min()),
        winning_days=float((returns > 0).mean()),
    )


# ---------- What can be reported on ----------

def sources(project) -> list[tuple[str, str]]:
    """(value, label) of every records folder with an equity curve: the live account first, then the backtests,
    newest first."""
    found = [(LIVE_SOURCE, f"Live: {project.describe_mode()}")]
    folder = project.data_root / BACKTEST_FOLDER
    if folder.is_dir():
        for run in sorted(folder.iterdir(), reverse=True):
            if (run / records.EQUITY_FILE).exists():
                found.append((run.name, f"Backtest from {run.name}"))
    return found


def folder_of(project, source: str) -> Path:
    return project.live_records if source == LIVE_SOURCE else project.backtest_records(source)
