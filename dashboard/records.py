"""Reading what the C++ trader saves in a records folder (data/live, data/backtest/<day>, ...).

    status.json        what it is doing now (StatusWriter): state, account, each stock, latest events
    trades.csv         every order: sent, blocked by the risk checks, rejected by the broker, adjustments
    positions.csv      what we hold
    performance.json   our P&L estimate and the broker's figures
    equity.csv         the account's value once a minute

Every reader copes with a missing or half-written file (the trader may be writing it) by returning nothing.
"""

from __future__ import annotations

import json
from pathlib import Path

import pandas as pd

STATUS_FILE = "status.json"
TRADES_FILE = "trades.csv"
POSITIONS_FILE = "positions.csv"
PERFORMANCE_FILE = "performance.json"
EQUITY_FILE = "equity.csv"
LOG_FILE = "trader.log"

# trades.csv's status column
SENT, BLOCKED, REJECTED, ADJUSTMENT, DRY_RUN = "sent", "blocked", "rejected", "adjustment", "dry_run"
CHANGED_POSITION = {SENT, ADJUSTMENT, DRY_RUN}   # the orders that changed what we hold

TRADE_COLUMNS = ["our_id", "time", "sent_at_utc", "strategy", "ticker", "side", "type", "order_class", "quantity",
                 "price", "stop_loss", "take_profit", "status", "broker_status", "client_order_id",
                 "broker_order_id", "round_trip_ms", "note", "order_path", "requests", "send_ms", "connect_ms"]
ENTRY, EXIT = "entry", "exit"   # order_path: an exit cancels the position's stop loss and take profit first
EQUITY_COLUMNS = ["time", "equity"]
POSITION_COLUMNS = ["ticker", "quantity", "avg_price"]


def read_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return {}


def read_csv(path: Path, columns: list[str]) -> pd.DataFrame:
    try:
        frame = pd.read_csv(path, keep_default_na=False, na_values=[""])
    except (OSError, ValueError, pd.errors.ParserError):
        return pd.DataFrame(columns=columns)
    for column in columns:
        if column not in frame:
            frame[column] = None
    return frame[columns]


def status(folder: Path) -> dict:
    return read_json(folder / STATUS_FILE)


def performance(folder: Path) -> dict:
    return read_json(folder / PERFORMANCE_FILE)


def trades(folder: Path) -> pd.DataFrame:
    """Every order, oldest first, with `day` (New York date) and numbers as numbers."""
    frame = read_csv(folder / TRADES_FILE, TRADE_COLUMNS)
    for column in ["quantity", "price", "stop_loss", "take_profit", "round_trip_ms", "requests", "send_ms", "connect_ms"]:
        frame[column] = pd.to_numeric(frame[column], errors="coerce")
    frame["time"] = frame["time"].astype(str)
    frame["day"] = frame["time"].str[:10]
    return frame


def positions(folder: Path) -> pd.DataFrame:
    frame = read_csv(folder / POSITIONS_FILE, POSITION_COLUMNS)
    for column in ["quantity", "avg_price"]:
        frame[column] = pd.to_numeric(frame[column], errors="coerce")
    return frame[frame["quantity"].fillna(0) != 0].reset_index(drop=True)


def equity(folder: Path) -> pd.DataFrame:
    """The account's value each minute, with `day`."""
    frame = read_csv(folder / EQUITY_FILE, EQUITY_COLUMNS)
    frame["equity"] = pd.to_numeric(frame["equity"], errors="coerce")
    frame = frame.dropna(subset=["equity"])
    frame["time"] = frame["time"].astype(str)
    frame["day"] = frame["time"].str[:10]
    return frame.reset_index(drop=True)


def log_tail(folder: Path, lines: int) -> str:
    """The last `lines` lines of the trader's output (trader.log, written when the dashboard starts it)."""
    try:
        with (folder / LOG_FILE).open("rb") as file:
            file.seek(0, 2)
            size = file.tell()
            file.seek(max(0, size - lines * 200))
            text = file.read().decode("utf-8", errors="replace")
    except OSError:
        return ""
    return "\n".join(text.splitlines()[-lines:])
