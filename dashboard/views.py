"""What each part of the dashboard shows, as plain data: headline figures, tables (text plus a tone for
colouring) and charts (Plotly figure dictionaries). app.py only lays these out, so everything here can be
tested without a browser or Dash."""

from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from datetime import datetime

import pandas as pd

from . import comparison, metrics, records
from .project import Project

# Tones the stylesheet colours (assets/style.css)
POSITIVE, NEGATIVE, WARNING, MUTED, PLAIN = "positive", "negative", "warning", "muted", ""
NOTHING = "-"

# Chart colours: the terminal look of assets/style.css
PANEL = "#000000"
GRID = "#141a1f"
TEXT = "#9aa5ad"
LIVE_COLOUR = "#3ad7f0"
BACKTEST_COLOUR = "#f5d547"
UP_COLOUR = "#4ade80"
DOWN_COLOUR = "#f87171"
CHART_FONT = "JetBrains Mono, Menlo, monospace"
CHART_MARGIN = {"l": 8, "r": 64, "t": 10, "b": 28}
CHART_HEIGHT = 300
DAILY_PNL_ZOOM_KEY = "daily-pnl"   # one chart of every day: it keeps its zoom as new days arrive

# The tape: an arrow for each side, as on a trading terminal
BUY_ARROW, SELL_ARROW = "▲", "▼"
TODAY_KPI = "Today"   # shown large in the P&L panel, the rest along the status bar

RECENT_EVENTS = 40
RECENT_TRADES = 60


# ---------- Formatting ----------

def missing(value) -> bool:
    return value is None or (isinstance(value, float) and math.isnan(value))


def money(value, decimals: int = 2) -> str:
    if missing(value):
        return NOTHING
    sign = "-" if value < 0 and round(abs(value), decimals) > 0 else ""
    return f"{sign}${abs(value):,.{decimals}f}"


def signed_money(value, decimals: int = 2) -> str:
    if missing(value):
        return NOTHING
    if round(abs(value), decimals) == 0:
        return f"+${0:.{decimals}f}"
    return f"{'+' if value > 0 else '-'}${abs(value):,.{decimals}f}"


def percent(value, decimals: int = 2, signed: bool = False) -> str:
    if missing(value):
        return NOTHING
    if round(abs(value), decimals) == 0:
        value = 0.0
    return f"{value:+.{decimals}f}%" if signed else f"{value:.{decimals}f}%"


def number(value, decimals: int = 2) -> str:
    if missing(value):
        return NOTHING
    if float(value).is_integer() and decimals:
        return f"{value:,.0f}"
    return f"{value:,.{decimals}f}"


def milliseconds(value) -> str:
    if missing(value):
        return NOTHING
    return f"{value:.1f} ms" if value >= 1 else f"{value * 1000:.0f} µs"


def text_or(value, otherwise: str = "") -> str:
    """A text column's value, or `otherwise` when it is empty (pandas reads empty cells as NaN)."""
    return otherwise if missing(value) or value == "" else str(value)


def tone_of(value) -> str:
    if missing(value) or abs(value) < 1e-9:
        return PLAIN
    return POSITIVE if value > 0 else NEGATIVE


# ---------- Tables ----------

@dataclass
class Table:
    columns: list[str]
    rows: list[list[tuple[str, str]]] = field(default_factory=list)   # each cell: (text, tone)
    empty: str = "Nothing yet"   # shown instead of the table when there are no rows; "" keeps just the headers

    def add(self, *cells) -> None:
        self.rows.append([cell if isinstance(cell, tuple) else (str(cell), PLAIN) for cell in cells])


@dataclass
class Kpi:
    title: str
    value: str
    detail: str = ""
    tone: str = PLAIN


# ---------- The header ----------

@dataclass
class Header:
    state: str
    state_tone: str
    mode: str
    mode_tone: str
    market: str
    market_open: bool
    running: bool
    warnings: list[str]


def header(project: Project, status: dict, now: datetime) -> Header:
    hours = metrics.market_hours(status, now)
    health = metrics.health(status, now, hours.is_open)
    state_tone = {"running": POSITIVE, "starting": WARNING, "stopping": WARNING}.get(health.state, MUTED)
    if health.state == "not responding":
        state_tone = NEGATIVE
    return Header(state=health.state.capitalize(), state_tone=state_tone,
                  mode=project.describe_mode(), mode_tone=NEGATIVE if project.is_real_money else
                  (WARNING if project.is_dry_run else POSITIVE),
                  market=hours.label + ("" if hours.exact or not hours.label else " (regular hours)"),
                  market_open=hours.is_open, running=health.running, warnings=health.warnings)


# ---------- The live tab ----------

def today(status: dict, trades: pd.DataFrame, equity: pd.DataFrame) -> str:
    """The trading day the live tab shows: the latest bar's, else the latest record's."""
    for day in [str(status.get("last_bar_time", ""))[:10], equity["day"].max() if not equity.empty else "",
                trades["day"].max() if not trades.empty else ""]:
        if isinstance(day, str) and len(day) == 10:
            return day
    return ""


def live_kpis(status: dict, trades: pd.DataFrame, day: str) -> list[Kpi]:
    account = status.get("account", {}) if status else {}
    estimate = status.get("estimate", {}) if status else {}
    has_account = bool(account.get("available"))
    equity = account.get("equity") if has_account else estimate.get("equity")
    today_pnl = account.get("today_pnl") if has_account else estimate.get("total_pnl")
    orders = trades[trades["day"] == day]
    latency = metrics.latency(orders)
    held = [s for s in (status or {}).get("stocks", []) if abs(s.get("position", {}).get("quantity", 0)) > 0]
    gross = sum(abs(s["position"].get("market_value", 0)) for s in held)
    sent = int((orders["status"] == records.SENT).sum())
    blocked = int((orders["status"] == records.BLOCKED).sum())
    rejected = int((orders["status"] == records.REJECTED).sum())
    return [
        Kpi("Equity", money(equity), "Alpaca" if has_account else "our estimate"),
        Kpi(TODAY_KPI, signed_money(today_pnl), percent(today_pnl / (equity - today_pnl) * 100, signed=True)
            if not missing(today_pnl) and not missing(equity) and equity - today_pnl else "", tone_of(today_pnl)),
        Kpi("Realised P&L", signed_money(estimate.get("realised_pnl")),
            f"{estimate.get('closed_trades', 0)} closed, {percent(estimate.get('win_rate_percent'), 0)} won",
            tone_of(estimate.get("realised_pnl"))),
        Kpi("Positions", str(len(held)), f"{money(gross, 0)} gross"),
        Kpi("Orders", str(sent), f"{blocked} blocked, {rejected} rejected",
            WARNING if rejected else PLAIN),
        Kpi("Latency", milliseconds(latency["median"]),
            f"p95 {milliseconds(latency['p95'])}, max {milliseconds(latency['max'])}" if latency["orders"] else
            "no orders yet"),
    ]


def positions_table(status: dict, risk_config: dict) -> Table:
    frame, _ = metrics.exposure(status, risk_config)
    prices = {s["ticker"]: s for s in (status or {}).get("stocks", [])}
    table = Table(["Stock", "Qty", "Avg", "Last", "Unrealised", "% eq"], empty="")
    for row in frame.itertuples():
        stock = prices.get(row.ticker, {})
        position = stock.get("position", {})
        over = row.percent_of_equity > row.cap_percent
        table.add(row.ticker, number(row.quantity, 0), number(position.get("avg_price")), number(stock.get("price")),
                  (signed_money(row.unrealised_pnl), tone_of(row.unrealised_pnl)),
                  (percent(row.percent_of_equity, 1), WARNING if over else PLAIN))
    return table


def strategy_table(status: dict) -> Table:
    """One line a strategy: where its indicator sits between the levels that would trade."""
    table = Table(["Stock", "Strategy", "Low < now < high", "State"], empty="NO BARS YET")
    for stock in (status or {}).get("stocks", []):
        for i, levels in enumerate(stock.get("strategies", []) or [{}]):
            between = " < ".join(str(levels.get(key) or NOTHING) for key in ("lower", "current", "upper"))
            table.add(stock["ticker"] if i == 0 else "", levels.get("label", NOTHING), between,
                      (levels.get("state") or "warming up", PLAIN if levels.get("ready") else MUTED))
    return table


def trades_table(trades: pd.DataFrame, day: str, limit: int = RECENT_TRADES) -> Table:
    """The tape: today's orders, newest first."""
    table = Table(["Time", "", "Stock", "Qty", "Price", "Strategy", "Status", "Note"], empty="NO ORDERS TODAY")
    tones = {records.SENT: POSITIVE, records.BLOCKED: WARNING, records.REJECTED: NEGATIVE, records.ADJUSTMENT: MUTED}
    for trade in trades[trades["day"] == day].iloc[::-1].head(limit).itertuples():
        buying = trade.side == "buy"
        side_tone = POSITIVE if buying else NEGATIVE
        table.add(trade.time[11:19], (BUY_ARROW if buying else SELL_ARROW, side_tone), (trade.ticker, side_tone),
                  number(trade.quantity, 0), (number(trade.price), side_tone), text_or(trade.strategy, NOTHING),
                  (trade.status, tones.get(trade.status, PLAIN)), text_or(trade.note))
    return table


def events(status: dict, limit: int = RECENT_EVENTS) -> list[tuple[str, str, str]]:
    """(time, stock, text), newest first"""
    found = (status or {}).get("events", [])[-limit:]
    return [(str(e.get("time", ""))[11:16], e.get("ticker", ""), e.get("text", "")) for e in reversed(found)]


# ---------- Charts ----------

def _layout(zoom_key: str, height: int = CHART_HEIGHT) -> dict:
    """Black, faint grid, prices on the right: the panel around the chart carries its title.

    zoom_key: the chart keeps the viewer's zoom through new data while this stays the same (Plotly's uirevision),
    e.g. the day it shows; another day starts zoomed out. A double-click or a page refresh also zooms out.
    """
    axis = {"gridcolor": GRID, "zeroline": False, "linecolor": GRID, "tickfont": {"size": 10}}
    return {"height": height, "margin": CHART_MARGIN, "paper_bgcolor": PANEL, "plot_bgcolor": PANEL,
            "uirevision": zoom_key,
            "font": {"color": TEXT, "family": CHART_FONT, "size": 10},
            "xaxis": axis, "yaxis": {**axis, "side": "right", "tickprefix": "$"},
            "legend": {"orientation": "h", "y": 1.0, "x": 0, "bgcolor": "rgba(0,0,0,0)"},
            "hovermode": "x unified", "hoverlabel": {"bgcolor": PANEL, "font": {"family": CHART_FONT}}}


def equity_figure(curves: list[tuple[str, pd.DataFrame, str]], zoom_key: str) -> dict:
    """curves: (name, equity frame, colour). zoom_key: see _layout, usually the day shown."""
    data = [{"type": "scatter", "mode": "lines", "name": name, "x": list(frame["time"]),
             "y": [round(v, 2) for v in frame["equity"]], "line": {"color": colour, "width": 1.5, "shape": "hv"}}
            for name, frame, colour in curves if not frame.empty]
    return {"data": data, "layout": {**_layout(zoom_key), "showlegend": len(data) > 1}}


def daily_pnl_figure(history: pd.DataFrame) -> dict:
    days = history.dropna(subset=["pnl"]).iloc[::-1]
    return {"data": [{"type": "bar", "x": list(days["day"]), "y": [round(v, 2) for v in days["pnl"]],
                      "marker": {"color": [UP_COLOUR if v >= 0 else DOWN_COLOUR for v in days["pnl"]]},
                      "name": "P&L"}],
            "layout": {**_layout(DAILY_PNL_ZOOM_KEY), "showlegend": False}}


# ---------- Backtest vs live ----------

def comparison_table(result: dict) -> Table:
    table = Table(["", "Live", "Backtest", "Difference"])
    live, test = result["live"], result["backtest"]
    rows = [("P&L", "pnl", signed_money), ("P&L %", "pnl_percent", lambda v: percent(v, signed=True)),
            ("Orders sent", "orders_sent", lambda v: number(v, 0)), ("Closed trades", "closed_trades", lambda v: number(v, 0)),
            ("Win rate", "win_rate_percent", lambda v: percent(v, 0)),
            ("Max drawdown", "max_drawdown_percent", lambda v: percent(v))]
    for label, key, show in rows:
        a, b = live.get(key), test.get(key)
        difference = a - b if not missing(a) and not missing(b) else None
        table.add(label, (show(a), tone_of(a) if key.startswith("pnl") else PLAIN),
                  (show(b), tone_of(b) if key.startswith("pnl") else PLAIN),
                  (show(difference) if key != "pnl" else signed_money(difference), tone_of(difference)
                   if key.startswith("pnl") else PLAIN))
    table.add("Fills", f"{result['matched']} matched", f"{result['live_only']} live only",
              f"{result['backtest_only']} backtest only")
    return table


def fills_table(result: dict) -> Table:
    table = Table(["Stock", "Side", "Live time", "Backtest time", "Live price", "Backtest price", "Difference", ""],
                  empty="No fills on this day")
    tones = {comparison.MATCH: POSITIVE, comparison.LIVE_ONLY: WARNING, comparison.BACKTEST_ONLY: WARNING}
    for row in result["fills"].itertuples():
        table.add(row.ticker, row.side, str(row.time_live)[11:19] if not missing(row.time_live) else NOTHING,
                  str(row.time_backtest)[11:19] if not missing(row.time_backtest) else NOTHING,
                  number(row.price_live), number(row.price_backtest),
                  (number(row.price_difference, 4), tone_of(-row.price_difference if row.side == "buy"
                                                            else row.price_difference)
                   if not missing(row.price_difference) else NOTHING),
                  (row.result, tones.get(row.result, PLAIN)))
    return table


def comparison_figure(result: dict) -> dict:
    return equity_figure([("Live", result["live_equity"], LIVE_COLOUR),
                          ("Backtest", result["backtest_equity"], BACKTEST_COLOUR)], result["day"])


# ---------- History ----------

def history_kpis(history: pd.DataFrame) -> list[Kpi]:
    days = history.dropna(subset=["pnl"])
    if days.empty:
        return [Kpi("Days traded", "0")]
    total = float(days["pnl"].sum())
    best, worst = days.loc[days["pnl"].idxmax()], days.loc[days["pnl"].idxmin()]
    return [Kpi("Days traded", str(len(days))),
            Kpi("Total P&L", signed_money(total), "", tone_of(total)),
            Kpi("Winning days", percent((days["pnl"] > 0).mean() * 100, 0), f"{int((days['pnl'] > 0).sum())} of {len(days)}"),
            Kpi("Best day", signed_money(best["pnl"]), best["day"], POSITIVE),
            Kpi("Worst day", signed_money(worst["pnl"]), worst["day"], NEGATIVE if worst["pnl"] < 0 else PLAIN),
            Kpi("Worst drawdown", percent(days["max_drawdown_percent"].max()), "within a day")]


def history_table(history: pd.DataFrame) -> Table:
    table = Table(["Day", "P&L", "%", "Drawdown", "Orders", "Won"], empty="NO TRADING DAYS YET")
    for row in history.itertuples():
        table.add(row.day, (signed_money(row.pnl), tone_of(row.pnl)),
                  (percent(row.pnl_percent, signed=True), tone_of(row.pnl_percent)), percent(row.max_drawdown_percent),
                  (number(row.orders_sent, 0), WARNING if row.rejected else PLAIN), percent(row.win_rate_percent, 0))
    return table


# ---------- Config ----------

def portfolio_table(project: Project) -> Table:
    table = Table(["Stock", "Strategy", "Settings"], empty="No portfolio in config/strategy.json")
    for stock in project.strategy.get("portfolio", []):
        strategies = stock.get("strategy", stock.get("strategies", []))
        for strategy in strategies if isinstance(strategies, list) else [strategies]:
            settings = ", ".join(f"{k} {v}" for k, v in strategy.items() if k not in ("name", "active"))
            table.add(stock.get("ticker", NOTHING), strategy.get("name", NOTHING), settings)
    return table


def risk_text(project: Project) -> str:
    risk = project.risk
    shown = {key: value for key, value in risk.items() if not key.startswith("_") and key != "risk_tuning"}
    return json.dumps(shown, indent=2)
