"""Figures the dashboard shows, worked out from the trader's records (see records.py)."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, time, timedelta, timezone

import pandas as pd

from . import records

try:
    from zoneinfo import ZoneInfo
    NEW_YORK = ZoneInfo("America/New_York")
except Exception:   # no time zone data: New York hours can't be worked out
    NEW_YORK = None

# Health
HEARTBEAT_STALE_SECONDS = 10       # the trader rewrites status.json every second while running
BAR_STALE_SECONDS = 150            # a minute bar per stock, plus Alpaca's few seconds of delay
RUNNING, STOPPED = "running", "stopped"

# Regular New York hours, when Alpaca's own clock isn't in status.json
MARKET_OPEN = time(9, 30)
MARKET_CLOSE = time(16, 0)
SATURDAY = 5

# Exposure: risk.json's defaults (src/risk/RiskEngine.hpp)
DEFAULT_MAX_STOCK_PERCENT = 20.0
DEFAULT_MAX_PORTFOLIO_PERCENT = 100.0
FIXED_SIZE = "FIXED_SIZE"

LATENCY_PERCENTILE = 0.95
SAME_QUANTITY = 1e-9


def parse_utc(text: str) -> datetime | None:
    try:
        return datetime.fromisoformat(str(text).replace("Z", "+00:00"))
    except ValueError:
        return None


def seconds_since(text: str, now: datetime) -> float | None:
    then = parse_utc(text)
    return (now - then).total_seconds() if then else None


def describe_duration(seconds: float) -> str:
    """e.g. 3725 -> "1h 2m", 95000 -> "1d 2h 23m", 40 -> "40s" """
    seconds = int(max(0, seconds))
    days, rest = divmod(seconds, 86400)
    hours, rest = divmod(rest, 3600)
    minutes, secs = divmod(rest, 60)
    if days:
        return f"{days}d {hours}h {minutes}m"
    if hours:
        return f"{hours}h {minutes}m"
    if minutes:
        return f"{minutes}m"
    return f"{secs}s"


# ---------- Health ----------

@dataclass
class Health:
    running: bool             # the trader is up and writing its status
    state: str                # starting / running / stopping / stopped, or "not running"
    heartbeat_age: float | None
    last_bar_age: float | None
    connected: bool
    warnings: list[str]


def health(status: dict, now: datetime, market_open: bool) -> Health:
    """Is the trader alive, connected and getting bars?"""
    if not status:
        return Health(False, "not running", None, None, False, [])
    state = status.get("state", STOPPED)
    heartbeat = seconds_since(status.get("updated_utc", ""), now)
    bar_age = seconds_since(status.get("last_bar_received_utc", ""), now)
    alive = state != STOPPED and heartbeat is not None and heartbeat < HEARTBEAT_STALE_SECONDS
    warnings = []
    if state != STOPPED and not alive:
        warnings.append("No heartbeat: the trader may have crashed")
    if alive and not status.get("connected", True):
        warnings.append("Stream disconnected (reconnecting)")
    if alive and market_open and (bar_age is None or bar_age > BAR_STALE_SECONDS):
        warnings.append("No bars recently: the feed may be stale")
    return Health(alive, state if alive or state == STOPPED else "not responding", heartbeat, bar_age,
                  bool(status.get("connected", True)), warnings)


# ---------- Market hours ----------

@dataclass
class MarketHours:
    is_open: bool
    label: str        # e.g. "Open, closes in 2h 5m"
    exact: bool       # from Alpaca's clock (knows holidays) rather than regular hours


def market_hours(status: dict, now: datetime) -> MarketHours:
    clock = status.get("market_clock", {}) if status else {}
    if clock.get("known"):
        is_open = bool(clock.get("is_open"))
        next_change = parse_utc(clock.get("next_close_utc" if is_open else "next_open_utc", ""))
        if next_change:
            # Alpaca's clock is from the last refresh (every 5 minutes): roll it on if a change has passed
            if next_change > now:
                return MarketHours(is_open, _describe(is_open, next_change - now), True)
    return _regular_hours(now)


def _describe(is_open: bool, until: timedelta) -> str:
    return f"{'Open, closes' if is_open else 'Closed, opens'} in {describe_duration(until.total_seconds())}"


def _regular_hours(now: datetime) -> MarketHours:
    if NEW_YORK is None:
        return MarketHours(False, "Market hours unknown", False)
    local = now.astimezone(NEW_YORK)
    open_today = local.replace(hour=MARKET_OPEN.hour, minute=MARKET_OPEN.minute, second=0, microsecond=0)
    close_today = local.replace(hour=MARKET_CLOSE.hour, minute=MARKET_CLOSE.minute, second=0, microsecond=0)
    weekday = local.weekday() < SATURDAY
    if weekday and open_today <= local < close_today:
        return MarketHours(True, _describe(True, _between(local, close_today)), False)
    next_open = open_today if weekday and local < open_today else open_today + timedelta(days=1)
    while next_open.weekday() >= SATURDAY:
        next_open += timedelta(days=1)
    return MarketHours(False, _describe(False, _between(local, next_open)), False)


def _between(earlier: datetime, later: datetime) -> timedelta:
    # In UTC: subtracting two New York times directly ignores a change of clocks between them
    return later.astimezone(timezone.utc) - earlier.astimezone(timezone.utc)


# ---------- Orders ----------

def latency(trades: pd.DataFrame) -> dict:
    """How long Alpaca took to answer each order sent: count, median, 95th percentile, max (milliseconds)."""
    sent = trades[(trades["status"] == records.SENT) & (trades["round_trip_ms"] > 0)]["round_trip_ms"]
    if sent.empty:
        return {"orders": 0, "median": None, "p95": None, "max": None}
    return {"orders": int(sent.size), "median": float(sent.median()),
            "p95": float(sent.quantile(LATENCY_PERCENTILE)), "max": float(sent.max())}


# ---------- Exposure ----------

def exposure(status: dict, risk_config: dict) -> tuple[pd.DataFrame, dict]:
    """Each position's value and share of equity against the risk engine's caps, and the portfolio total."""
    risk = risk_config.get("risk", {})
    stock_cap = float(risk.get("max_stock_percent", DEFAULT_MAX_STOCK_PERCENT))
    portfolio_cap = float(risk.get("max_portfolio_percent", DEFAULT_MAX_PORTFOLIO_PERCENT))
    account = (status or {}).get("account", {})
    equity = float(account.get("equity") or (status or {}).get("estimate", {}).get("equity") or 0)

    rows = []
    for stock in (status or {}).get("stocks", []):
        position = stock.get("position", {})
        quantity = float(position.get("quantity", 0))
        if abs(quantity) < SAME_QUANTITY:
            continue
        value = float(position.get("market_value", quantity * stock.get("price", 0)))
        share = abs(value) / equity * 100 if equity > 0 else 0.0
        rows.append({"ticker": stock["ticker"], "quantity": quantity, "market_value": value,
                     "percent_of_equity": share, "cap_percent": stock_cap,
                     "unrealised_pnl": float(position.get("unrealised_pnl", 0))})
    frame = pd.DataFrame(rows, columns=["ticker", "quantity", "market_value", "percent_of_equity", "cap_percent",
                                        "unrealised_pnl"])
    total = float(frame["market_value"].abs().sum()) if not frame.empty else 0.0
    summary = {"equity": equity, "gross_exposure": total,
               "gross_percent": total / equity * 100 if equity > 0 else 0.0, "cap_percent": portfolio_cap,
               "engine": risk.get("name", FIXED_SIZE),
               "share_limit": risk_config.get("max_position_size") if risk.get("name", FIXED_SIZE) == FIXED_SIZE else None}
    return frame, summary


# ---------- Closed trades and days ----------

def closed_trades(trades: pd.DataFrame) -> pd.DataFrame:
    """Each position closed (or reduced), with its P&L, by replaying the orders that changed what we held
    through an average-cost book (as the trader's own P&L tracker does)."""
    held: dict[str, tuple[float, float]] = {}   # ticker -> (quantity, average price)
    rows = []
    for trade in trades[trades["status"].isin(records.CHANGED_POSITION)].itertuples():
        quantity = float(trade.quantity or 0)
        if quantity <= 0 or pd.isna(trade.price):
            continue
        signed = quantity if trade.side == "buy" else -quantity
        current, average = held.get(trade.ticker, (0.0, 0.0))
        if current == 0 or (current > 0) == (signed > 0):
            total = abs(current) + quantity
            held[trade.ticker] = (current + signed, (abs(current) * average + quantity * trade.price) / total)
            continue
        closing = min(quantity, abs(current))
        direction = 1.0 if current > 0 else -1.0
        pnl = closing * (trade.price - average) * direction
        rows.append({"day": trade.day, "time": trade.time, "ticker": trade.ticker, "quantity": closing,
                     "entry": average, "exit": float(trade.price), "pnl": pnl,
                     "return_percent": (trade.price - average) / average * 100 * direction if average else 0.0})
        left = current + (closing if signed > 0 else -closing)
        remaining = quantity - closing
        if remaining > 0:
            held[trade.ticker] = (remaining if signed > 0 else -remaining, float(trade.price))
        else:
            held[trade.ticker] = (left, average if abs(left) > SAME_QUANTITY else 0.0)
    return pd.DataFrame(rows, columns=["day", "time", "ticker", "quantity", "entry", "exit", "pnl", "return_percent"])


def day_history(trades: pd.DataFrame, equity: pd.DataFrame) -> pd.DataFrame:
    """One row per trading day: the account's start and end, P&L, worst fall, orders, wins. Newest first."""
    closed = closed_trades(trades)
    days = sorted(set(equity["day"]) | set(trades[trades["day"].str.len() == 10]["day"]))
    rows = []
    previous_close = None
    for day in days:
        curve = equity[equity["day"] == day]["equity"]
        orders = trades[trades["day"] == day]
        exits = closed[closed["day"] == day]
        start = previous_close if previous_close is not None else (float(curve.iloc[0]) if not curve.empty else None)
        end = float(curve.iloc[-1]) if not curve.empty else None
        peak = curve.cummax()
        rows.append({
            "day": day,
            "start_equity": start,
            "end_equity": end,
            "pnl": end - start if start is not None and end is not None else None,
            "pnl_percent": (end - start) / start * 100 if start and end is not None else None,
            "max_drawdown_percent": float(((peak - curve) / peak).max() * 100) if not curve.empty else None,
            "orders_sent": int((orders["status"] == records.SENT).sum()),
            "blocked": int((orders["status"] == records.BLOCKED).sum()),
            "rejected": int((orders["status"] == records.REJECTED).sum()),
            "closed_trades": int(len(exits)),
            "win_rate_percent": float((exits["pnl"] > 0).mean() * 100) if len(exits) else None,
            "realised_pnl": float(exits["pnl"].sum()) if len(exits) else 0.0,
        })
        if end is not None:
            previous_close = end
    return pd.DataFrame(rows).iloc[::-1].reset_index(drop=True) if rows else pd.DataFrame(
        columns=["day", "start_equity", "end_equity", "pnl", "pnl_percent", "max_drawdown_percent", "orders_sent",
                 "blocked", "rejected", "closed_trades", "win_rate_percent", "realised_pnl"])
