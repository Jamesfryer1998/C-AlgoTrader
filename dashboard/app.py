"""The trading dashboard: a Dash web page over the C++ trader's records, with start / stop / flatten.

    python -m dashboard            then open http://127.0.0.1:8050

It only listens on this computer (127.0.0.1): the page can start live trading with your Alpaca keys.
What each panel shows comes from views.py; this file only lays it out and wires the buttons.
The look (black, monospace, cyan-framed panels) is in assets/style.css.
"""

from __future__ import annotations

import argparse
import hashlib
import logging
from datetime import datetime, timezone

import dash_bootstrap_components as dbc
from dash import ClientsideFunction, Dash, Input, Output, State, ctx, dcc, html, no_update

from . import comparison, metrics, records, views
from .project import Project
from .trader import TraderControl

HOST = "127.0.0.1"   # this computer only
PORT = 8050
LIVE_REFRESH_MS = 2000
SLOW_REFRESH_MS = 15000
CLOCK_TICK_MS = 250        # in the browser only: the seconds turn over on time
BACKTEST_POLL_MS = 2000    # only while a backtest is running
MESSAGE_MS = 4000
LOG_LINES = 200
TITLE = "AlgoTrader"
REQUEST_LOGGER = "werkzeug"   # Flask's server prints a line for every request; errors only
FONTS = "https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;600&family=VT323&display=swap"
SEPARATOR = "◆"

GRAPH_CONFIG = {"displayModeBar": False}   # drag to zoom, double-click to zoom back out

LIVE, COMPARE, HISTORY, STRATEGIES, SYSTEM = "live", "compare", "history", "strategies", "system"
TABS = [(LIVE, "1 LIVE"), (COMPARE, "2 BACKTEST VS LIVE"), (HISTORY, "3 HISTORY"), (STRATEGIES, "4 STRATEGIES"),
        (SYSTEM, "5 SYSTEM")]


# ---------- Building blocks ----------

def table(view: views.Table) -> html.Div:
    if not view.rows and view.empty:
        return html.Div(view.empty, className="empty")
    head = html.Thead(html.Tr([html.Th(column) for column in view.columns]))
    body = html.Tbody([html.Tr([html.Td(text, className=tone) for text, tone in row]) for row in view.rows])
    return html.Div(html.Table([head, body], className="data-table"), className="table-wrap")


def panel(number: int, title: str, body, extra=None, tall: bool = False) -> html.Div:
    """A cyan-framed box with its numbered title tab on the top edge, like '2 CHART'."""
    tab = html.Div([html.Span(f"{number} {title}".upper(), className="panel-tab"), extra or ""],
                   className="panel-heading")
    return html.Div([tab, html.Div(body, className="panel-body")], className="panel tall" if tall else "panel")


def chart(figure: dict) -> dcc.Graph:
    return dcc.Graph(figure=figure, config=GRAPH_CONFIG, className="chart")


def graph(id_: str) -> dcc.Graph:
    """A chart whose figure a callback fills in (and later updates, keeping the viewer's zoom)."""
    return dcc.Graph(id=id_, config=GRAPH_CONFIG, className="chart")


def big_number(kpi: views.Kpi) -> html.Div:
    return html.Div([html.Div(kpi.value, className=f"big-number {kpi.tone}"),
                     html.Div(kpi.detail, className=f"big-detail {kpi.tone}")])


def event_list(events: list[tuple[str, str, str]]):
    if not events:
        return html.Div("NOTHING YET", className="empty")
    return [html.Div([html.Span(time, className="muted"), html.Span(stock, className="accent"), html.Span(text)],
                     className="event") for time, stock, text in events]


class SentParts:
    """What the browser shows already: a fingerprint of each part's data, kept in the page (a dcc.Store).

    A refresh sends only the parts whose data changed and leaves the rest alone, so the browser redraws less
    and a chart keeps the viewer's zoom. A reloaded page starts with none, so it gets everything.
    """

    def __init__(self, fingerprints: dict | None):
        self.fingerprints = dict(fingerprints or {})

    def update(self, part: str, data, render):
        """render(data) if `part`'s data changed since the browser last got it, else Dash's no_update."""
        fingerprint = hashlib.sha1(repr(data).encode()).hexdigest()
        if self.fingerprints.get(part) == fingerprint:
            return no_update
        self.fingerprints[part] = fingerprint
        return render(data)


def segments(parts: list) -> list:
    """Status-bar pieces with a ◆ between them."""
    spaced = []
    for part in parts:
        if spaced:
            spaced.append(html.Span(SEPARATOR, className="sep"))
        spaced.append(part)
    return spaced


def kpi_segment(kpi: views.Kpi) -> html.Span:
    return html.Span([html.Span(kpi.title.upper(), className="label"), html.Span(kpi.value, className=kpi.tone)],
                     title=kpi.detail, className="segment")


def key(label: str, id_: str, tone: str) -> html.Button:
    return html.Button(label, id=id_, className=f"key {tone}", n_clicks=0)


def confirm(id_: str, title: str, body: str, button: str, tone: str) -> dbc.Modal:
    return dbc.Modal([html.Div(title.upper(), className="panel-tab"),
                      html.Div(body, id=f"{id_}-body", className="modal-text"),
                      html.Div([key("CANCEL", f"{id_}-cancel", views.MUTED),
                                key(button.upper(), f"{id_}-confirm", tone)], className="modal-keys")],
                     id=f"{id_}-modal", is_open=False, centered=True, content_class_name="terminal-modal")


# ---------- Layout ----------

def layout() -> html.Div:
    status_bar = html.Div([
        html.Div([html.Div(segments([html.Span(TITLE.upper(), className="brand"), html.Span(id="mode"),
                                     html.Span(id="state"), html.Span(id="market"), html.Span(id="clock-ny"),
                                     html.Span(id="clock-local")]), className="bar-left"),
                  html.Div([key("START", "start", views.POSITIVE), key("STOP", "stop", views.MUTED),
                            key("FLATTEN", "flatten", views.NEGATIVE)], className="bar-keys")], className="bar-line"),
        html.Div(id="kpis", className="bar-line kpi-line"),
    ], className="status-bar")

    tabs = dbc.Tabs([dbc.Tab(label=label, tab_id=tab) for tab, label in TABS], id="tabs", active_tab=LIVE,
                    className="terminal-tabs")

    compare_controls = html.Div([
        html.Span("DAY", className="label"),
        dbc.Select(id="compare-day", className="day-picker"),
        key("RUN BACKTEST", "run-backtest", views.WARNING),
    ], className="controls")

    live_tab = html.Div([
        dbc.Row([dbc.Col(panel(1, "Equity today", graph("live-equity"), tall=True), lg=8),
                 dbc.Col([panel(2, "P&L today", html.Div(id="live-pnl")),
                          panel(3, "Positions", html.Div(id="live-positions"))], lg=4)], className="g-2"),
        dbc.Row([dbc.Col(panel(4, "Tape", html.Div(id="live-tape")), lg=8),
                 dbc.Col([panel(5, "Strategies", html.Div(id="live-strategies")),
                          panel(6, "Events", html.Div(id="live-events", className="events"))], lg=4)], className="g-2"),
    ], id="live-tab")

    history_tab = html.Div([
        html.Div(id="history-stats", className="stat-line"),
        dbc.Row([dbc.Col(panel(1, "Daily P&L", graph("history-daily")), lg=7),
                 dbc.Col(panel(2, "Days", html.Div(id="history-days")), lg=5)], className="g-2"),
        panel(3, "One day", graph("history-day-equity"), extra=dbc.Select(id="history-day", className="day-picker")),
    ], id="history-tab", style={"display": "none"})

    strategies_tab = html.Div([
        dbc.Row([dbc.Col(panel(1, "Realised P&L by strategy", graph("strategies-pnl")), lg=7),
                 dbc.Col(panel(2, "By stock", html.Div(id="strategies-by-stock")), lg=5)], className="g-2"),
        panel(3, "Strategies, every recorded day", html.Div(id="strategies-table")),
        html.Div("Realised P&L of closed trades only, each credited to the strategy that opened it "
                 "(stop losses and take profits filled at the broker included).", className="empty"),
    ], id="strategies-tab", style={"display": "none"})

    return html.Div([
        status_bar,
        html.Div(id="warnings"),
        html.Div(id="message"),
        tabs,
        live_tab,
        html.Div([compare_controls, html.Div(id="compare-body")], id="compare-tab", style={"display": "none"}),
        history_tab,
        strategies_tab,
        html.Div(id="system-tab", style={"display": "none"}),
        confirm("start", "Start trading", "", "Start", views.POSITIVE),
        confirm("flatten", "Flatten and stop",
                "Send an order closing every position at market, then stop the trader?", "Flatten", views.NEGATIVE),
        dcc.Interval(id="fast", interval=LIVE_REFRESH_MS),
        dcc.Interval(id="slow", interval=SLOW_REFRESH_MS),
        dcc.Interval(id="clock-tick", interval=CLOCK_TICK_MS),
        dcc.Interval(id="backtest-poll", interval=BACKTEST_POLL_MS, disabled=True),
        dcc.Store(id="live-sent"),
        dcc.Store(id="history-sent"),
        dcc.Store(id="strategies-sent"),
    ], className="page")


# ---------- The app ----------

def create_app(project: Project) -> Dash:
    control = TraderControl(project)
    app = Dash(__name__, title=TITLE, update_title=None, external_stylesheets=[dbc.themes.BOOTSTRAP, FONTS],
               suppress_callback_exceptions=True)
    app.layout = layout

    # The clocks tick in the browser (assets/clock.js)
    app.clientside_callback(ClientsideFunction("clock", "tick"), Output("clock-ny", "children"),
                            Output("clock-local", "children"), Input("clock-tick", "n_intervals"))

    def now() -> datetime:
        return datetime.now(timezone.utc)

    def live_day():
        folder = project.live_records
        status, trades, equity = records.status(folder), records.trades(folder), records.equity(folder)
        return status, trades, equity, views.today(status, trades, equity)

    @app.callback(*[Output(f"{tab}-tab", "style") for tab, _ in TABS], Input("tabs", "active_tab"))
    def show_tab(active):
        return [{"display": "block" if tab == active else "none"} for tab, _ in TABS]

    @app.callback(Output("mode", "children"), Output("mode", "className"),
                  Output("state", "children"), Output("state", "className"),
                  Output("market", "children"), Output("kpis", "children"), Output("warnings", "children"),
                  Output("start", "disabled"), Output("stop", "disabled"), Output("flatten", "disabled"),
                  Output("live-equity", "figure"), Output("live-pnl", "children"), Output("live-positions", "children"),
                  Output("live-tape", "children"), Output("live-strategies", "children"),
                  Output("live-events", "children"), Output("live-sent", "data"),
                  Input("fast", "n_intervals"), Input("tabs", "active_tab"), State("live-sent", "data"))
    def refresh_live(_, active, sent_before):
        """The status bar, and the Live tab when it's showing: one read of the records for both."""
        status, trades, equity, day = live_day()
        sent = SentParts(sent_before)
        head = views.header(project, status, now())
        kpis = views.live_kpis(status, trades, day)
        header = (head.mode.upper(), head.mode_tone, head.state.upper(), head.state_tone, head.market.upper(),
                  sent.update("kpis", [k for k in kpis if k.title != views.TODAY_KPI],
                              lambda bar: segments([kpi_segment(k) for k in bar])),
                  sent.update("warnings", head.warnings,
                              lambda texts: [html.Div(f"! {text}", className="warning-line") for text in texts]),
                  head.running, not head.running, not head.running)
        if active != LIVE:   # the hidden tab keeps what it shows, and catches up when it's opened again
            return (*header, *[no_update] * 6, sent.fingerprints)

        todays_equity = equity[equity["day"] == day]
        live = (sent.update("equity", (day, todays_equity.values.tolist()),
                            lambda _: views.equity_figure([("Equity", todays_equity, views.LIVE_COLOUR)], day)),
                sent.update("pnl", next(k for k in kpis if k.title == views.TODAY_KPI), big_number),
                sent.update("positions", views.positions_table(status, project.risk), table),
                sent.update("tape", views.trades_table(trades, day), table),
                sent.update("strategies", views.strategy_table(status), table),
                sent.update("events", views.events(status), event_list))
        return (*header, *live, sent.fingerprints)

    # ----- Start / stop / flatten -----

    @app.callback(Output("start-modal", "is_open"), Output("start-body", "children"),
                  Output("start-confirm", "className"),
                  Input("start", "n_clicks"), Input("start-cancel", "n_clicks"), Input("start-confirm", "n_clicks"),
                  prevent_initial_call=True)
    def toggle_start(*_):
        if ctx.triggered_id != "start":
            return False, no_update, no_update
        warning = "This account trades REAL MONEY. " if project.is_real_money else ""
        body = f"{warning}Start the trader ({project.describe_mode()}), as config/general.json says?"
        return True, body, f"key {views.NEGATIVE if project.is_real_money else views.POSITIVE}"

    @app.callback(Output("flatten-modal", "is_open"),
                  Input("flatten", "n_clicks"), Input("flatten-cancel", "n_clicks"),
                  Input("flatten-confirm", "n_clicks"), prevent_initial_call=True)
    def toggle_flatten(*_):
        return ctx.triggered_id == "flatten"

    @app.callback(Output("message", "children"),
                  Input("start-confirm", "n_clicks"), Input("stop", "n_clicks"), Input("flatten-confirm", "n_clicks"),
                  prevent_initial_call=True)
    def act(*_):
        if not ctx.triggered or not ctx.triggered[0]["value"]:
            return no_update
        action = {"start-confirm": control.start, "stop": control.stop, "flatten-confirm": control.flatten}
        result = action[ctx.triggered_id]()
        return dbc.Alert(f"> {result}", duration=MESSAGE_MS, className="message-line")

    # ----- Backtest vs live -----

    @app.callback(Output("compare-day", "options"), Output("compare-day", "value"),
                  Input("tabs", "active_tab"), State("compare-day", "value"))
    def compare_days(active, chosen):
        if active != COMPARE:
            return no_update, no_update
        days = comparison.traded_days(project.live_records)
        return [{"label": day, "value": day} for day in days], chosen if chosen in days else (days[0] if days else None)

    @app.callback(Output("compare-body", "children"), Output("backtest-poll", "disabled"),
                  Input("compare-day", "value"), Input("backtest-poll", "n_intervals"),
                  Input("run-backtest", "n_clicks"), State("tabs", "active_tab"))
    def refresh_compare(day, _, __, active):
        """Redrawn when the day changes or its backtest starts, then every couple of seconds while it runs."""
        if ctx.triggered_id == "run-backtest" and day:
            control.run_backtest(day, comparison.live_start(project.live_records, day))
        job = control.jobs.get(day) if day else None
        backtesting = bool(job and job.state == "running")
        if active != COMPARE:
            return no_update, not backtesting
        if not day:
            return html.Div("NO TRADED DAYS RECORDED YET", className="empty"), True
        result = comparison.compare(project.live_records, project.backtest_records(day), day)
        notes = []
        if job and job.state == "running":
            notes.append(html.Div(f"> Backtesting {day} {job.start.describe()}...", className="warning-line"))
        elif job and job.state == "failed":
            notes.append(panel(0, "Backtest failed", html.Pre(job.output[-2000:], className="log")))
        if not result["has_backtest"]:
            notes.append(html.Div("NO BACKTEST OF THIS DAY YET: PRESS RUN BACKTEST", className="empty"))
            return html.Div(notes), not backtesting
        return html.Div(notes + [
            dbc.Row([dbc.Col(panel(1, "Summary", table(views.comparison_table(result))), lg=5),
                     dbc.Col(panel(2, "P&L since the open: live vs backtest", chart(views.comparison_figure(result))), lg=7)],
                    className="g-2"),
            panel(3, "Fills, side by side", table(views.fills_table(result))),
        ]), not backtesting

    # ----- History -----

    @app.callback(Output("history-stats", "children"), Output("history-daily", "figure"),
                  Output("history-days", "children"), Output("history-day-equity", "figure"),
                  Output("history-day", "options"), Output("history-day", "value"), Output("history-sent", "data"),
                  Input("tabs", "active_tab"), Input("slow", "n_intervals"), Input("history-day", "value"),
                  State("history-sent", "data"))
    def refresh_history(active, _, chosen_day, sent_before):
        if active != HISTORY:
            return (*[no_update] * 6, sent_before)
        sent = SentParts(sent_before)
        trades, equity = records.trades(project.live_records), records.equity(project.live_records)
        history = metrics.day_history(trades, equity)
        days = list(history["day"])
        day = chosen_day if chosen_day in days else (days[0] if days else None)
        intraday = equity[equity["day"] == day] if day else equity.iloc[0:0]
        return (sent.update("stats", views.history_kpis(history),
                            lambda kpis: segments([kpi_segment(k) for k in kpis])),
                sent.update("daily", history.values.tolist(), lambda _: views.daily_pnl_figure(history)),
                sent.update("days", views.history_table(history), table),
                sent.update("day", (day, intraday.values.tolist()),
                            lambda _: views.equity_figure([("Equity", intraday, views.LIVE_COLOUR)], day or "")),
                sent.update("options", days, lambda names: [{"label": d, "value": d} for d in names]),
                day if day != chosen_day else no_update,
                sent.fingerprints)

    # ----- Strategies -----

    @app.callback(Output("strategies-pnl", "figure"), Output("strategies-by-stock", "children"),
                  Output("strategies-table", "children"), Output("strategies-sent", "data"),
                  Input("tabs", "active_tab"), Input("slow", "n_intervals"), State("strategies-sent", "data"))
    def refresh_strategies(active, _, sent_before):
        if active != STRATEGIES:
            return no_update, no_update, no_update, sent_before
        sent = SentParts(sent_before)
        trades = records.trades(project.live_records)
        closed = metrics.closed_trades(trades)
        return (sent.update("pnl", closed.values.tolist(), lambda _: views.strategy_pnl_figure(closed)),
                sent.update("by-stock", views.strategy_stock_table(metrics.strategy_by_stock(trades)), table),
                sent.update("table", views.strategies_table(metrics.strategy_performance(trades)), table),
                sent.fingerprints)

    # ----- System: config and the trader's output -----

    @app.callback(Output("system-tab", "children"), Input("tabs", "active_tab"), Input("slow", "n_intervals"))
    def refresh_system(active, _):
        if active != SYSTEM:
            return no_update
        log = records.log_tail(project.live_records, LOG_LINES) or "No output yet"
        return html.Div([
            dbc.Row([dbc.Col(panel(1, "Portfolio  config/strategy.json", table(views.portfolio_table(project))), lg=6),
                     dbc.Col(panel(2, "Risk  config/risk.json", html.Pre(views.risk_text(project), className="log")),
                             lg=6)], className="g-2"),
            panel(3, "Trader log", html.Pre(log, className="log")),
        ])

    return app


def main() -> None:
    parser = argparse.ArgumentParser(description="AlgoTrader's web dashboard (this computer only)")
    parser.add_argument("--port", type=int, default=PORT)
    args = parser.parse_args()
    project = Project.find()
    logging.getLogger(REQUEST_LOGGER).setLevel(logging.ERROR)
    print(f"Dashboard for {project.root} on http://{HOST}:{args.port}")
    create_app(project).run(host=HOST, port=args.port, debug=False)
