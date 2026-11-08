"""A traded day next to a backtest of the same day: did live trading do what the backtest says it would?

The backtest (algo_trader_app --backtest DAY) runs the same strategies on that day's saved bars with the
simulated broker, so differences come from fills, slippage, timing and anything the simulation leaves out.
"""

from __future__ import annotations

from pathlib import Path

import pandas as pd

from . import metrics, records

MATCH, LIVE_ONLY, BACKTEST_ONLY = "matched", "live only", "backtest only"


def summary(trades: pd.DataFrame, equity: pd.DataFrame, day: str) -> dict:
    """One day's headline figures from a records folder."""
    history = metrics.day_history(trades[trades["day"] == day], equity[equity["day"] == day])
    if history.empty:
        return {"pnl": None, "pnl_percent": None, "orders_sent": 0, "blocked": 0, "closed_trades": 0,
                "win_rate_percent": None, "max_drawdown_percent": None}
    row = history.iloc[0]
    return {key: (None if pd.isna(row[key]) else row[key]) for key in
            ["pnl", "pnl_percent", "orders_sent", "blocked", "closed_trades", "win_rate_percent", "max_drawdown_percent"]}


def match_fills(live: pd.DataFrame, backtest: pd.DataFrame) -> pd.DataFrame:
    """Pair each stock's fills in order (1st live buy of KO with the 1st backtest buy of KO, ...), so the
    table shows how far apart they were in time and price, and which happened on one side only."""
    def fills(frame: pd.DataFrame) -> pd.DataFrame:
        changed = frame[frame["status"].isin(records.CHANGED_POSITION)].copy()
        changed["n"] = changed.groupby(["ticker", "side"]).cumcount()
        return changed[["ticker", "side", "n", "time", "quantity", "price", "strategy"]]

    joined = fills(live).merge(fills(backtest), on=["ticker", "side", "n"], how="outer", suffixes=("_live", "_backtest"))
    joined["result"] = MATCH
    joined.loc[joined["time_backtest"].isna(), "result"] = LIVE_ONLY
    joined.loc[joined["time_live"].isna(), "result"] = BACKTEST_ONLY
    joined["price_difference"] = joined["price_live"] - joined["price_backtest"]
    first_time = joined["time_live"].fillna(joined["time_backtest"])
    return joined.assign(sort_time=first_time).sort_values(["sort_time", "ticker"]).drop(columns=["sort_time", "n"]) \
                 .reset_index(drop=True)


def compare(live_folder: Path, backtest_folder: Path, day: str) -> dict:
    """Everything the comparison tab shows for one day."""
    live_trades, live_equity = records.trades(live_folder), records.equity(live_folder)
    test_trades, test_equity = records.trades(backtest_folder), records.equity(backtest_folder)
    fills = match_fills(live_trades[live_trades["day"] == day], test_trades[test_trades["day"] == day])
    return {
        "day": day,
        "live": summary(live_trades, live_equity, day),
        "backtest": summary(test_trades, test_equity, day),
        "live_equity": live_equity[live_equity["day"] == day],
        "backtest_equity": test_equity[test_equity["day"] == day],
        "fills": fills,
        "matched": int((fills["result"] == MATCH).sum()),
        "live_only": int((fills["result"] == LIVE_ONLY).sum()),
        "backtest_only": int((fills["result"] == BACKTEST_ONLY).sum()),
        "has_backtest": not test_equity[test_equity["day"] == day].empty,
    }


def traded_days(live_folder: Path) -> list[str]:
    """Days with live records, newest first."""
    days = set(records.equity(live_folder)["day"]) | set(records.trades(live_folder)["day"])
    return sorted((day for day in days if len(day) == 10), reverse=True)
