"""What each part of the dashboard shows, as plain data: headline figures, tables (text plus a tone for
colouring) and charts (Plotly figure dictionaries). app.py only lays these out, so everything here can be
tested without a browser or Dash."""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from datetime import datetime

import pandas as pd

from . import comparison, metrics, picks, records, report, research, server
from .project import Project

# Tones the stylesheet colours (assets/style.css)
POSITIVE, NEGATIVE, WARNING, MUTED, PLAIN = "positive", "negative", "warning", "muted", ""
NOTHING = "-"

# Chart colours: the terminal look of assets/style.css
PANEL = "#000000"
GRID = "#141a1f"
TEXT = "#9aa5ad"
# Chart lines are the theme's hues stepped into the band that reads on black (checked with the dataviz skill's
# validator: lightness, colour-blind separation and contrast all pass). The frames stay the brighter cyan.
LIVE_COLOUR = "#1aa3b8"
BACKTEST_COLOUR = "#b38612"
# Profit and loss. Red against green is hard for some colour-blind viewers, so a chart never shows the sign by
# colour alone: bars sit above or below zero, and heatmap cells carry their signed value.
UP_COLOUR = "#4ade80"
DOWN_COLOUR = "#f87171"
NEUTRAL_COLOUR = "#2b3238"   # the middle of the red-to-green scale: "nothing" is grey, never a hue
REFERENCE_COLOUR = "#6b7680"  # reference lines (a mean, the normal line on the Q-Q plot)
CHART_FONT = "JetBrains Mono, Menlo, monospace"
CHART_MARGIN = {"l": 8, "r": 64, "t": 10, "b": 28}
CHART_HEIGHT = 300
DAILY_PNL_ZOOM_KEY = "daily-pnl"   # one chart of every day: it keeps its zoom as new days arrive
STRATEGY_PNL_ZOOM_KEY = "strategy-pnl"
SIGNED_DOLLARS = "+$,.2f"   # Plotly's axis format for P&L: +$12.50, -$3.00
# A line per strategy, in this order (validated as a set): none green or red, which mean profit and loss here.
# More strategies than colours fold into one "Other" line rather than reusing a colour.
STRATEGY_COLOURS = ["#1aa3b8", "#d9702a", "#c45bd1", "#b38612", "#3987e5"]
PICKS_COLOUR = "#c45bd1"   # the weekly picks' line, beside LIVE_COLOUR and BACKTEST_COLOUR (validated as a set)
PICKS_ZOOM_KEY = "picks"
OTHER_STRATEGIES = "Other"
OTHER_COLOUR = "#6b7680"

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
        Kpi("Latency", milliseconds(latency["median"]), _latency_detail(latency)),
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
    data = [_line(name, frame["time"], frame["equity"], colour) for name, frame, colour in curves if not frame.empty]
    return {"data": data, "layout": {**_layout(zoom_key), "showlegend": len(data) > 1}}


def pnl_since_start_figure(curves: list[tuple[str, pd.DataFrame, str]], zoom_key: str) -> dict:
    """Each equity curve as the P&L since its own first minute, so runs that began with different money
    (a backtest's starting capital, the live account) all start at $0 and can be compared line against line."""
    data = [_line(name, frame["time"], frame["equity"] - frame["equity"].iloc[0], colour)
            for name, frame, colour in curves if not frame.empty]
    layout = _layout(zoom_key)
    layout["yaxis"] = {**layout["yaxis"], "tickprefix": "", "tickformat": SIGNED_DOLLARS}
    return {"data": data, "layout": {**layout, "showlegend": len(data) > 1}}


def _line(name: str, times: pd.Series, values: pd.Series, colour: str) -> dict:
    return {"type": "scatter", "mode": "lines", "name": name, "x": list(times), "y": [round(v, 2) for v in values],
            "line": {"color": colour, "width": 1.5, "shape": "hv"}}


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
    """Live and the backtest as P&L since the day's start: the backtest begins with its starting capital,
    live with whatever the account held, and only how each day went is worth comparing."""
    return pnl_since_start_figure([("Live", result["live_equity"], LIVE_COLOUR),
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
            Kpi("Best day", signed_money(best["pnl"]), best["day"], tone_of(best["pnl"])),
            Kpi("Worst day", signed_money(worst["pnl"]), worst["day"], NEGATIVE if worst["pnl"] < 0 else PLAIN),
            Kpi("Worst drawdown", percent(days["max_drawdown_percent"].max()), "within a day")]


def history_table(history: pd.DataFrame) -> Table:
    table = Table(["Day", "P&L", "%", "Drawdown", "Orders", "Won"], empty="NO TRADING DAYS YET")
    for row in history.itertuples():
        table.add(row.day, (signed_money(row.pnl), tone_of(row.pnl)),
                  (percent(row.pnl_percent, signed=True), tone_of(row.pnl_percent)), percent(row.max_drawdown_percent),
                  (number(row.orders_sent, 0), WARNING if row.rejected else PLAIN), percent(row.win_rate_percent, 0))
    return table


# ---------- Strategies ----------

def strategies_table(performance: pd.DataFrame) -> Table:
    table = Table(["Strategy", "Stocks", "Orders", "Blocked", "Closed", "Won", "P&L", "Average", "Best", "Worst"],
                  empty="NO TRADES RECORDED YET")
    for row in performance.itertuples():
        table.add(row.strategy, row.stocks, number(row.orders_sent, 0), number(row.blocked, 0),
                  number(row.closed_trades, 0), percent(row.win_rate_percent, 0),
                  (signed_money(row.pnl), tone_of(row.pnl)), _signed_or_nothing(row.average_pnl),
                  _signed_or_nothing(row.best), _signed_or_nothing(row.worst))
    return table


def strategy_stock_table(by_stock: pd.DataFrame) -> Table:
    table = Table(["Strategy", "Stock", "Closed", "Won", "P&L", "Average"], empty="NO CLOSED TRADES YET")
    for row in by_stock.itertuples():
        table.add(row.strategy, row.ticker, number(row.closed_trades, 0), percent(row.win_rate_percent, 0),
                  (signed_money(row.pnl), tone_of(row.pnl)), _signed_or_nothing(row.average_pnl))
    return table


def strategy_pnl_figure(closed: pd.DataFrame) -> dict:
    """Each strategy's realised P&L adding up, trade by trade. Colours go to strategies by name, so a strategy
    keeps its colour; past the palette, the rest add up in one "Other" line."""
    names = sorted(closed["strategy"].dropna().unique())
    own_line = names[:len(STRATEGY_COLOURS)]
    data = [_line(name, closed[closed["strategy"] == name]["time"], closed[closed["strategy"] == name]["pnl"].cumsum(),
                  colour) for name, colour in zip(own_line, STRATEGY_COLOURS)]
    others = closed[closed["strategy"].isin(names[len(STRATEGY_COLOURS):])]
    if not others.empty:
        data.append(_line(OTHER_STRATEGIES, others["time"], others["pnl"].cumsum(), OTHER_COLOUR))
    return {"data": data, "layout": {**_layout(STRATEGY_PNL_ZOOM_KEY), "showlegend": True}}


# ---------- Report ----------

MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
PERCENT_AXIS = ".1%"
SIGNED_PERCENT_AXIS = "+.1%"
SIGNED_PERCENT_HOVER = "%{y:+.2%}"
HISTOGRAM_BINS = 30
QQ_MARKER_SIZE = 8
GAP_PIXELS = 2   # between heatmap cells and bars, so neighbouring fills stay apart


def report_kpis(summary: report.Summary) -> list[Kpi]:
    """The headline numbers above the report's charts."""
    def signed(value):
        return percent(None if value is None else value * 100, 2, signed=True)

    def ratio(value):
        return number(value) if value is not None else NOTHING
    too_short = f"needs {report.MIN_DAYS_TO_ANNUALISE} days"
    return [Kpi("Days", str(summary.days)),
            Kpi("Total return", signed(summary.total_return), "", tone_of(summary.total_return)),
            Kpi("Annualised", signed(summary.annualised_return), too_short, tone_of(summary.annualised_return)),
            Kpi("Volatility", percent(None if summary.volatility is None else summary.volatility * 100, 1),
                "annualised; " + too_short),
            Kpi("Sharpe", ratio(summary.sharpe), "annualised, no risk-free rate; " + too_short),
            Kpi("Sortino", ratio(summary.sortino), "annualised; " + too_short),
            Kpi("Max drawdown", signed(summary.max_drawdown), "from a closing high", tone_of(summary.max_drawdown)),
            Kpi("Best day", signed(summary.best_day), "", tone_of(summary.best_day)),
            Kpi("Worst day", signed(summary.worst_day), "", tone_of(summary.worst_day)),
            Kpi("Winning days", percent(None if summary.winning_days is None else summary.winning_days * 100, 0))]


def _percent_layout(zoom_key: str, signed: bool = True) -> dict:
    layout = _layout(zoom_key)
    layout["yaxis"] = {**layout["yaxis"], "tickprefix": "", "tickformat": SIGNED_PERCENT_AXIS if signed else PERCENT_AXIS}
    return layout


def _note_figure(text: str, zoom_key: str) -> dict:
    """An empty chart that says why it's empty."""
    layout = _layout(zoom_key)
    layout["xaxis"] = {**layout["xaxis"], "visible": False}
    layout["yaxis"] = {**layout["yaxis"], "visible": False}
    layout["annotations"] = [{"text": text.upper(), "showarrow": False, "font": {"color": REFERENCE_COLOUR},
                              "xref": "paper", "yref": "paper", "x": 0.5, "y": 0.5}]
    return {"data": [], "layout": layout}


def _too_short(days: pd.DataFrame, needed: int, zoom_key: str) -> dict | None:
    if len(days) >= needed:
        return None
    return _note_figure(f"Needs at least {needed} days ({len(days)} so far)", zoom_key)


def growth_figure(days: pd.DataFrame, zoom_key: str) -> dict:
    """The return since the start, each day's close."""
    if days.empty:
        return _note_figure("No equity recorded yet", zoom_key)
    line = _line("Return", days["day"], days["growth"], LIVE_COLOUR)
    line["hovertemplate"] = "%{x}  " + SIGNED_PERCENT_HOVER + "<extra></extra>"
    return {"data": [line], "layout": {**_percent_layout(zoom_key), "showlegend": False}}


def drawdown_figure(days: pd.DataFrame, zoom_key: str) -> dict:
    """How far each close sits below the highest close before it."""
    if days.empty:
        return _note_figure("No equity recorded yet", zoom_key)
    line = _line("Drawdown", days["day"], days["drawdown"], DOWN_COLOUR)
    line.update({"fill": "tozeroy", "fillcolor": "rgba(248,113,113,0.15)",
                 "hovertemplate": "%{x}  " + SIGNED_PERCENT_HOVER + "<extra></extra>"})
    return {"data": [line], "layout": {**_percent_layout(zoom_key), "showlegend": False}}


def monthly_heatmap(months: pd.DataFrame, zoom_key: str) -> dict:
    """A cell per month, red to green through grey, each labelled with its return."""
    if months.empty:
        return _note_figure("No equity recorded yet", zoom_key)
    values = [[None if pd.isna(v) else round(float(v), 6) for v in row] for row in months.values]
    labels = [["" if v is None else f"{v:+.1%}" for v in row] for row in values]
    biggest = max([abs(v) for row in values for v in row if v is not None] or [1])
    heatmap = {"type": "heatmap", "z": values, "x": MONTHS, "y": [str(year) for year in months.index],
               "text": labels, "texttemplate": "%{text}", "textfont": {"family": CHART_FONT, "size": 10},
               "colorscale": [[0, DOWN_COLOUR], [0.5, NEUTRAL_COLOUR], [1, UP_COLOUR]],
               "zmid": 0, "zmin": -biggest, "zmax": biggest, "showscale": False,
               "xgap": GAP_PIXELS, "ygap": GAP_PIXELS, "hoverongaps": False,
               "hovertemplate": "%{x} %{y}  %{z:+.2%}<extra></extra>"}
    layout = _layout(zoom_key)
    layout["yaxis"] = {**layout["yaxis"], "tickprefix": "", "type": "category", "autorange": "reversed"}
    layout["xaxis"] = {**layout["xaxis"], "type": "category", "showgrid": False}
    layout["hovermode"] = "closest"
    return {"data": [heatmap], "layout": layout}


def daily_returns_figure(days: pd.DataFrame, zoom_key: str) -> dict:
    """Each day's return as a bar above or below zero, with the mean dashed across."""
    if days.empty:
        return _note_figure("No equity recorded yet", zoom_key)
    bars = {"type": "bar", "x": list(days["day"]), "y": [round(v, 6) for v in days["return"]], "name": "Day",
            "marker": {"color": [UP_COLOUR if v >= 0 else DOWN_COLOUR for v in days["return"]]},
            "hovertemplate": "%{x}  " + SIGNED_PERCENT_HOVER + "<extra></extra>"}
    layout = _percent_layout(zoom_key)
    layout.update({"bargap": 0.25, "showlegend": False, "hovermode": "closest",
                   "shapes": [_mean_line("y", float(days["return"].mean()))]})
    return {"data": [bars], "layout": layout}


def distribution_figure(days: pd.DataFrame, zoom_key: str) -> dict:
    """How the daily returns spread, with their mean."""
    short = _too_short(days, report.MIN_DAYS_FOR_SHAPE, zoom_key)
    if short:
        return short
    histogram = {"type": "histogram", "x": [round(v, 6) for v in days["return"]], "nbinsx": HISTOGRAM_BINS,
                 "marker": {"color": LIVE_COLOUR, "line": {"color": PANEL, "width": GAP_PIXELS}},
                 "hovertemplate": "%{x}  %{y} days<extra></extra>"}
    layout = _layout(zoom_key)
    layout["xaxis"] = {**layout["xaxis"], "tickformat": SIGNED_PERCENT_AXIS}
    layout["yaxis"] = {**layout["yaxis"], "tickprefix": "", "title": {"text": "days"}}
    layout.update({"showlegend": False, "hovermode": "closest", "shapes": [_mean_line("x", float(days["return"].mean()))]})
    return {"data": [histogram], "layout": layout}


def qq_figure(days: pd.DataFrame, zoom_key: str) -> dict:
    """The sorted daily returns against where a normal distribution would put them: on the line means normal,
    ends bending away mean fatter tails (bigger days, good and bad, than a normal curve expects)."""
    short = _too_short(days, report.MIN_DAYS_FOR_SHAPE, zoom_key)
    if short:
        return short
    observed = sorted(days["return"])
    expected = report.normal_quantiles(len(observed))
    mean, deviation = float(days["return"].mean()), float(days["return"].std())
    points = {"type": "scatter", "mode": "markers", "x": [round(q, 3) for q in expected],
              "y": [round(v, 6) for v in observed], "name": "Days",
              "marker": {"color": LIVE_COLOUR, "size": QQ_MARKER_SIZE, "line": {"color": PANEL, "width": GAP_PIXELS}},
              "hovertemplate": "normal %{x:.2f}  actual " + SIGNED_PERCENT_HOVER + "<extra></extra>"}
    normal = {"type": "scatter", "mode": "lines", "x": [expected[0], expected[-1]],
              "y": [mean + deviation * expected[0], mean + deviation * expected[-1]], "name": "Normal",
              "line": {"color": REFERENCE_COLOUR, "width": 1, "dash": "dash"}, "hoverinfo": "skip"}
    layout = _percent_layout(zoom_key)
    layout["xaxis"] = {**layout["xaxis"], "title": {"text": "normal quantile"}}
    layout.update({"showlegend": False, "hovermode": "closest"})
    return {"data": [normal, points], "layout": layout}


def rolling_figure(window: pd.DataFrame, days: pd.DataFrame, zoom_key: str) -> dict:
    """The return over the last N days and their annualised volatility, both in %, on one axis."""
    short = _too_short(days, report.ROLLING_DAYS, zoom_key)
    if short:
        return short
    data = [_line(f"{report.ROLLING_DAYS}-day return", window["day"], window["return"], LIVE_COLOUR),
            _line(f"{report.ROLLING_DAYS}-day volatility (annualised)", window["day"], window["volatility"],
                  BACKTEST_COLOUR)]
    for line in data:
        line["hovertemplate"] = SIGNED_PERCENT_HOVER + "<extra>%{fullData.name}</extra>"
    return {"data": data, "layout": {**_percent_layout(zoom_key), "showlegend": True}}


def _mean_line(axis: str, mean: float) -> dict:
    """A dashed line across the chart at the mean (axis "y": horizontal, "x": vertical)."""
    across = "x" if axis == "y" else "y"
    return {"type": "line", "xref": "x" if axis == "x" else "paper", "yref": "y" if axis == "y" else "paper",
            f"{axis}0": mean, f"{axis}1": mean, f"{across}0": 0, f"{across}1": 1,
            "line": {"color": REFERENCE_COLOUR, "width": 1, "dash": "dash"}}


# ---------- Picks ----------

def screen_status(progress: picks.ScreenProgress, runs: list[str], now: float) -> tuple[str, float]:
    """The screen's line beside its button and how full its progress bar is (0 to 1)."""
    if progress.state == picks.SCREENING:
        left = progress.seconds_left(now)
        eta = f", about {metrics.describe_duration(left)} left" if left is not None else ", working out the time left"
        stock = f" on {progress.stock}" if progress.stock else ""
        return f"Screening {progress.done} of {progress.total} stocks{stock}{eta}", progress.fraction
    last = f"Last screened {runs[0]}" if runs else "Not screened yet"
    if progress.state == picks.FAILED:
        return f"The last screen failed (see data/screen/weekly/screen.log). {last}", 0.0
    return f"{last}. Next: automatically at the weekend, or now with the button", 0.0


def weekly_picks_table(table: pd.DataFrame) -> Table:
    """A screen's top picks and why: how each did tuning, then on the test month it never saw."""
    # Settings last: it's long, and the table trims its last column rather than push the numbers out of sight
    view = Table(["#", "Stock", "Strategy", "Test Sharpe", "Train Sharpe", "Buy & hold", "Test trades", "Settings"],
                 empty="NO PICKS YET: RUN A SCREEN")
    for row in table.itertuples():
        view.add(str(row.rank), row.symbol, row.strategy, (number(row.test_sharpe), tone_of(row.test_sharpe)),
                 number(row.train_sharpe), number(row.buy_hold_sharpe), number(row.test_closed_trades, 0),
                 row.parameters)
    return view


def picks_kpis(days: pd.DataFrame) -> list[Kpi]:
    """Totals over the days the picks were backtested next to live."""
    compared = days.dropna(subset=["live", "picks"])
    ahead = int((compared["picks_minus_live"] > 0).sum())
    live, chosen = float(compared["live"].sum()), float(compared["picks"].sum())
    return [Kpi("Days compared", str(len(compared))),
            Kpi("Live", signed_money(live), "the live account's P&L on those days", tone_of(live)),
            Kpi("Picks", signed_money(chosen), "the week's picks backtested on them", tone_of(chosen)),
            Kpi("Picks - live", signed_money(chosen - live), "", tone_of(chosen - live)),
            Kpi("Picks ahead", f"{ahead} of {len(compared)} days")]


def picks_figure(days: pd.DataFrame) -> dict:
    """P&L added up day by day: live, live's portfolio backtested, the week's picks backtested."""
    if days.empty:
        return _note_figure("No traded days yet", PICKS_ZOOM_KEY)
    lines = []
    for name, column, colour in [("Live", "live", LIVE_COLOUR), ("Live's portfolio backtested", "live_backtest",
                                                                  BACKTEST_COLOUR),
                                 ("Picks backtested", "picks", PICKS_COLOUR)]:
        known = days.dropna(subset=[column])
        if not known.empty:
            line = _line(name, known["day"], known[column].cumsum(), colour)
            line.update({"mode": "lines+markers", "marker": {"size": 8, "line": {"color": PANEL, "width": 2}},
                         "line": {**line["line"], "shape": "linear"},
                         "hovertemplate": "%{y:+$,.2f}<extra>%{fullData.name}</extra>"})
            lines.append(line)
    layout = _layout(PICKS_ZOOM_KEY)
    layout["yaxis"] = {**layout["yaxis"], "tickprefix": "", "tickformat": SIGNED_DOLLARS}
    return {"data": lines, "layout": {**layout, "showlegend": True}}


def picks_days_table(days: pd.DataFrame) -> Table:
    table = Table(["Day", "Live", "Live backtested", "Picks backtested", "Picks - live", "Picks from"],
                  empty="NO TRADED DAYS YET")
    for row in days.iloc[::-1].itertuples():   # newest first
        table.add(row.day, _signed_or_nothing(row.live), _signed_or_nothing(row.live_backtest),
                  _signed_or_nothing(row.picks), _signed_or_nothing(row.picks_minus_live), row.picks_run or NOTHING)
    return table


def _latency_detail(latency: dict) -> str:
    """The Latency figure's hover text: entries and exits apart (when recorded), then the slow end."""
    if not latency["orders"]:
        return "no orders yet"
    parts = []
    if latency["entry"] is not None:
        parts.append(f"entries {milliseconds(latency['entry'])}")
    if latency["exit"] is not None:
        parts.append(f"exits {milliseconds(latency['exit'])} ({number(latency['exit_requests'], 0)} requests)")
    parts.append(f"p95 {milliseconds(latency['p95'])}, max {milliseconds(latency['max'])}")
    return ", ".join(parts)


def latency_table(by_day: pd.DataFrame) -> Table:
    """Order latency day by day (medians), to see what a change to the trader did. Days before entries and exits
    were recorded apart show only the overall figures."""
    table = Table(["Day", "Orders", "All", "Entries", "Exits", "Requests / exit", "Order request", "p95", "Max"],
                  empty="NO ORDERS SENT YET")
    for row in by_day.itertuples():
        table.add(row.day, number(row.orders, 0), milliseconds(row.median), milliseconds(row.entry),
                  milliseconds(row.exit), number(row.exit_requests, 0) if not missing(row.exit_requests) else NOTHING,
                  milliseconds(row.send), milliseconds(row.p95), milliseconds(row.max))
    return table


def _signed_or_nothing(value) -> tuple[str, str]:
    return (NOTHING, MUTED) if missing(value) else (signed_money(value), tone_of(value))


# ---------- Config ----------

def portfolio_table(project: Project) -> Table:
    """What the trader's next Start will trade: config/strategy.json's portfolio, or the picks chosen instead."""
    table = Table(["Stock", "Strategy", "Settings"], empty="No portfolio in config/strategy.json")
    for stock in picks.portfolio_list(project):
        strategies = stock.get("strategy", stock.get("strategies", []))
        for strategy in strategies if isinstance(strategies, list) else [strategies]:
            settings = ", ".join(f"{k} {v}" for k, v in strategy.items() if k not in ("name", "active"))
            table.add(stock.get("ticker", NOTHING), strategy.get("name", NOTHING), settings)
    return table


def portfolio_status(running: str | None, upcoming: str) -> str:
    """"TRADING config/strategy.json  ◆  NEXT START picks from 2026-11-07" (running: None when stopped)."""
    now = f"TRADING {running}" if running else "STOPPED"
    return f"{now}  ◆  NEXT START {upcoming}"




# ---------- Research: the morning screen ----------

RESEARCH_ZOOM_KEY = "research"
IN_PLAY, RANDOM, BENCHMARK = "in play", "random", "SPY buy and hold"   # morning_app's line names
RANDOM_COLOUR = REFERENCE_COLOUR   # the random control is a reference line, grey, never a hue
BENCHMARK_COLOUR = BACKTEST_COLOUR
PERIODS = ["development", "validation", "holdout"]
PASS, FAIL = "PASS", "FAIL"
HUNDRED = 100   # morning_app saves returns as fractions; percent() shows percentages


def chosen_selection(report_json: dict) -> str:
    """The selection a report tests ("in play", "matched"); the other is RANDOM."""
    return report_json.get("chosen", IN_PLAY)


def research_status(progress: research.ResearchProgress, report_json: dict | None, now: float,
                    study: research.Study = research.MORNING) -> tuple[str, float]:
    """The line beside the research buttons and how full its bar is (0 to 1)."""
    if progress.state == research.RUNNING:
        left = progress.seconds_left(now)
        eta = f", about {metrics.describe_duration(left)} left" if left is not None else ""
        count = f" {progress.done} of {progress.total}" if progress.total else ""
        return f"{progress.stage or 'Starting'}:{count}{eta}", progress.fraction
    last = f"Last run {report_json['generated_utc'][:16].replace('T', ' ')} UTC" if report_json else "Not run yet"
    if progress.state == research.FAILED:
        return f"The last run failed (see data/{study.folder}/{study.log}). {last}", 0.0
    return f"{last}. A first run downloads what it needs and takes a while", 0.0


def research_kpis(report_json: dict) -> list[Kpi]:
    dates = report_json["dates"]
    opened = report_json.get("holdout_opened")
    holdout = f"opened {opened[:10]}" if opened else "still sealed"
    return [Kpi("Data", f"{dates['start']} to {dates['end']}"),
            Kpi("Stocks listed", number(report_json["universe_symbols"], 0)),
            Kpi("Days traded", number(report_json["days_traded"], 0)),
            Kpi(report_json.get("count_label", "Stocks a day").capitalize(), number(report_json["stocks_a_day"], 0)),
            Kpi("Round-trip cost", percent(report_json["round_trip_cost_percent"], 2)),
            Kpi("Holdout", holdout, f"after {dates['validation_end']}", WARNING if opened else PLAIN)]


def gates_table(report_json: dict) -> Table:
    """Each pass mark from the plan, with what the run measured."""
    table = Table(["Step", "Rule", "Period", "Verdict", "Checks"], empty="NO RESULTS YET")
    for gate in report_json.get("gates", []):
        failed = [check for check in gate["checks"] if not check["pass"]]
        checks = "; ".join(f"{'ok' if check['pass'] else 'no'}: {check['name']} ({check['detail']})"
                           for check in (failed or gate["checks"]))
        table.add(gate["step"].split(":")[0], gate["variant"] or NOTHING, gate["period"],
                  (PASS if gate["pass"] else FAIL, POSITIVE if gate["pass"] else NEGATIVE), checks)
    return table


def moves_table(report_json: dict) -> Table:
    """Step 1: how far each selection's stocks typically move, against what a round trip costs."""
    cost = report_json["round_trip_cost_percent"]
    table = Table(["Stocks", "Stock-days", "10-min move", "30-min move", "ATR", "Relative volume", "Move / cost"],
                  empty="NOT MEASURED FOR THIS STUDY")
    for selection, moves in report_json["step1"]["moves"].items():
        ratio = moves["move_10_minutes_percent"] / cost if cost else 0
        table.add(selection, number(moves["stock_days"], 0), f"{moves['move_10_minutes_percent']:.3f}%",
                  f"{moves['move_30_minutes_percent']:.3f}%", f"{moves['atr_percent']:.2f}%",
                  f"{moves['relative_volume']:.1f}x" if moves["relative_volume"] else NOTHING, f"{ratio:.1f}x")
    return table


def research_results_table(report_json: dict) -> Table:
    """Every rule on every selection in every period, the tested selection first."""
    order = {chosen_selection(report_json): 0, RANDOM: 1}
    rows = sorted(report_json.get("results", []),
                  key=lambda row: (PERIODS.index(row["period"]) if row["period"] in PERIODS else len(PERIODS),
                                   row["variant"] == BENCHMARK, row["variant"], order.get(row["selection"], 2)))
    table = Table(["Period", "Rule", "Stocks", "Return", "Sharpe", "Drawdown", "Trades", "Won", "$ a trade",
                   "Cost a trade", "Gross / costs", "Deflated"], empty="NO RESULTS YET")
    for row in rows:
        deflated = row.get("deflated_sharpe")
        table.add(row["period"], row["variant"], row["selection"],
                  (percent(row["total_return"] * HUNDRED, 1, signed=True), tone_of(row["total_return"])),
                  (number(row["sharpe"]), tone_of(row["sharpe"])), percent(row["max_drawdown"] * HUNDRED, 1),
                  number(row["trades"], 0), percent(row["win_rate"] * HUNDRED, 0) if row["trades"] else NOTHING,
                  (signed_money(row["avg_net_per_trade"]), tone_of(row["avg_net_per_trade"])) if row["trades"] else NOTHING,
                  money(row["avg_cost_per_trade"]) if row["trades"] else NOTHING,
                  number(row["gross_to_cost"]) if row["trades"] else NOTHING,
                  number(deflated) if deflated is not None else NOTHING)
    return table


def research_variants(report_json: dict | None) -> list[str]:
    if not report_json:
        return []
    names = []
    for curve in report_json.get("curves", []):
        if curve["variant"] != BENCHMARK and curve["variant"] not in names:
            names.append(curve["variant"])
    return names


def research_figure(report_json: dict | None, variant: str | None) -> dict:
    """One rule's account on the stocks in play, on random stocks, and SPY bought and held, as the return since
    the start, with the development / validation / holdout boundaries marked."""
    if not report_json or not report_json.get("curves"):
        return _note_figure("Run the research to see results", RESEARCH_ZOOM_KEY)
    lines, chosen = [], chosen_selection(report_json)
    for curve in report_json["curves"]:
        if curve["variant"] == variant and curve["selection"] == chosen:
            name, colour = f"{variant}, {chosen}", LIVE_COLOUR
        elif curve["variant"] == variant and curve["selection"] == RANDOM:
            name, colour = f"{variant}, random", RANDOM_COLOUR
        elif curve["variant"] == BENCHMARK:
            name, colour = BENCHMARK, BENCHMARK_COLOUR
        else:
            continue
        points = curve["points"]
        line = _line(name, [day for day, _ in points], [value - 1 for _, value in points], colour)
        line["y"] = [round(value - 1, 4) for _, value in points]
        line["line"] = {**line["line"], "shape": "linear"}
        line["hovertemplate"] = "%{x}  " + SIGNED_PERCENT_HOVER + "<extra>%{fullData.name}</extra>"
        lines.append(line)
    layout = _percent_layout(f"{RESEARCH_ZOOM_KEY}-{variant}")
    dates = report_json["dates"]
    layout["shapes"] = [{"type": "line", "xref": "x", "yref": "paper", "x0": day, "x1": day, "y0": 0, "y1": 1,
                         "line": {"color": REFERENCE_COLOUR, "width": 1, "dash": "dot"}}
                        for day in (dates["development_end"], dates["validation_end"])]
    layout["annotations"] = [{"text": label, "xref": "x", "yref": "paper", "x": day, "y": 1, "showarrow": False,
                              "xanchor": "left", "font": {"color": REFERENCE_COLOUR, "size": 9}}
                             for label, day in (("VALIDATION", dates["development_end"]),
                                                ("HOLDOUT", dates["validation_end"]))]
    return {"data": lines, "layout": {**layout, "showlegend": True}}


def leaderboard_summary(report_json: dict) -> str:
    """timing_app's search in a line: how many rules, and whether development's ranking held up."""
    board = report_json.get("leaderboard")
    if not board:
        return ""
    ahead = board["ahead_of_mix"]
    return (f"{number(board['rules_tried'], 0)} rules tried, {number(board['eligible'], 0)} eligible. Ahead of holding "
            f"{board['symbol']} at the same exposure without timing: {percent(ahead['development'] * HUNDRED, 0)} in "
            f"development, {percent(ahead['validation'] * HUNDRED, 0)} in validation. How well development's ranking "
            f"predicted validation's (rank correlation, 1 is perfectly): {number(board['edge_rank_correlation'])}")


def families_table(report_json: dict) -> Table:
    """Each family of rules: how its eligible rules did, as medians, so no single lucky rule speaks for it."""
    table = Table(["Family", "Tried", "Eligible", "Best edge, dev", "Median edge, dev", "Median edge, validation",
                   "Ahead in validation"], empty="NOT USED BY THIS STUDY")
    for family in (report_json.get("leaderboard") or {}).get("families", []):
        table.add(family["family"].replace("_", " "), number(family["tried"], 0), number(family["eligible"], 0),
                  _edge(family["best_development_edge"]), _edge(family["median_development_edge"]),
                  _edge(family["median_validation_edge"]),
                  percent(family["validation_ahead"] * HUNDRED, 0) if family["eligible"] else NOTHING)
    return table


def leaderboard_table(report_json: dict) -> Table:
    """The best rules on development, with validation for the finalists (*) only."""
    table = Table(["#", "Rule", "Exposure", "CAGR, dev", "Edge, dev", "Of SPY's, dev", "CAGR, validation",
                   "Edge, validation", "Of SPY's, validation", "Beat random timings"], empty="NOT USED BY THIS STUDY")
    for row in (report_json.get("leaderboard") or {}).get("top", []):
        development, validation = row["development"], row.get("validation")
        later = ([(percent(validation["cagr"] * HUNDRED, 1, signed=True), tone_of(validation["cagr"])),
                  _edge(validation["edge"]), percent(validation["capture"] * HUNDRED, 0),
                  percent(row["random_percentile"]["validation"] * HUNDRED, 0)]
                 if validation else [NOTHING] * 4)
        table.add(f"{row['rank']}{' *' if row['finalist'] else ''}", row["rule"].replace("_", " "),
                  percent(development["exposure"] * HUNDRED, 0),
                  (percent(development["cagr"] * HUNDRED, 1, signed=True), tone_of(development["cagr"])),
                  _edge(development["edge"]), percent(development["capture"] * HUNDRED, 0), *later)
    return table


def _edge(value: float) -> tuple[str, str]:
    """What timing added a year over the same exposure held without it."""
    return percent(value * HUNDRED, 1, signed=True), tone_of(value)


# ---------- System: the server ----------

LIMIT_WARNING_SHARE = 0.8   # past this much of the data limit, the oldest files are about to be deleted


def size(value: float) -> str:
    """Bytes as "812 MB", "3.4 GB"."""
    for unit, scale in (("GB", server.GIGABYTE), ("MB", 1024 ** 2), ("KB", server.KILOBYTE)):
        if value >= scale:
            return f"{value / scale:.1f} {unit}" if unit == "GB" else f"{value / scale:.0f} {unit}"
    return f"{value:.0f} B"


def data_table(sizes: list[server.FolderSize], limit: int) -> Table:
    """data/ folder by folder, against the limit beyond which the oldest files are deleted."""
    total = sum(folder.bytes for folder in sizes)
    table = Table(["Folder", "Size", "Files", "Of the limit"], empty="NO DATA YET")
    for folder in sizes:
        table.add(folder.name, size(folder.bytes), f"{folder.files:,}", percent(folder.bytes / limit * 100, 1))
    if sizes:
        share = total / limit
        tone = NEGATIVE if share >= 1 else WARNING if share >= LIMIT_WARNING_SHARE else PLAIN
        table.add(("All of data/", tone), (size(total), tone), f"{sum(f.files for f in sizes):,}",
                  (f"{percent(share * 100, 1)} of {size(limit)}", tone))
    return table


def server_table(disk: server.Disk | None, memory: server.Memory | None, load: tuple | None,
                 uptime: float | None, deployed: dict | None, cores: int) -> Table:
    """The machine under the dashboard, one fact a row."""
    table = Table(["", ""], empty="")
    if disk:
        share = disk.used / disk.total if disk.total else 0
        table.add("Disk", (f"{size(disk.free)} free of {size(disk.total)} ({percent(share * 100, 0)} used)",
                           WARNING if share >= LIMIT_WARNING_SHARE else PLAIN))
    if memory:
        table.add("Memory", f"{size(memory.available)} free of {size(memory.total)}")
    if load:
        busy = load[0] >= cores
        table.add("Load", (f"{load[0]:.2f} / {load[1]:.2f} / {load[2]:.2f} (1 / 5 / 15 min, {cores} cores)",
                           WARNING if busy else PLAIN))
    if uptime is not None:
        table.add("Up", metrics.describe_duration(uptime))
    if deployed:
        branch = f" on {deployed['branch']}" if deployed.get("branch") else ""
        table.add("Deployed", f"{deployed['commit']}{branch}, {deployed['deployed_utc'][:16].replace('T', ' ')} UTC")
        table.add("", deployed.get("subject", ""))
    else:
        table.add("Deployed", "not by deploy.sh (no DEPLOYED.json)")
    return table
