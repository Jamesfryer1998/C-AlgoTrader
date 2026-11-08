"""The trading dashboard: a Dash web page over the C++ trader's records, with start / stop / flatten.

    python -m dashboard            then open http://127.0.0.1:8050

It only listens on this computer (127.0.0.1): the page can start live trading with your Alpaca keys.
What each panel shows comes from views.py; this file only lays it out and wires the buttons.
The look (black, monospace, cyan-framed panels) is in assets/style.css.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone

import dash_bootstrap_components as dbc
from dash import Dash, Input, Output, State, ctx, dcc, html, no_update

from . import comparison, metrics, records, views
from .project import Project
from .trader import TraderControl

HOST = "127.0.0.1"   # this computer only
PORT = 8050
LIVE_REFRESH_MS = 2000
SLOW_REFRESH_MS = 15000
MESSAGE_MS = 4000
LOG_LINES = 200
TITLE = "AlgoTrader"
FONTS = "https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;600&family=VT323&display=swap"
SEPARATOR = "◆"

LIVE, COMPARE, HISTORY, SYSTEM = "live", "compare", "history", "system"
TABS = [(LIVE, "1 LIVE"), (COMPARE, "2 BACKTEST VS LIVE"), (HISTORY, "3 HISTORY"), (SYSTEM, "4 SYSTEM")]


# ---------- Building blocks ----------

def table(view: views.Table) -> html.Div:
    if not view.rows:
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
    return dcc.Graph(figure=figure, config={"displayModeBar": False}, className="chart")


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
        html.Span(id="backtest-state", className="muted"),
    ], className="controls")

    history_day = panel(3, "One day", html.Div(id="history-day-chart"),
                        extra=dbc.Select(id="history-day", className="day-picker"))

    return html.Div([
        status_bar,
        html.Div(id="warnings"),
        html.Div(id="message"),
        tabs,
        html.Div(id="live-tab"),
        html.Div([compare_controls, html.Div(id="compare-body")], id="compare-tab", style={"display": "none"}),
        html.Div([html.Div(id="history-body"), history_day], id="history-tab", style={"display": "none"}),
        html.Div(id="system-tab", style={"display": "none"}),
        confirm("start", "Start trading", "", "Start", views.POSITIVE),
        confirm("flatten", "Flatten and stop",
                "Send an order closing every position at market, then stop the trader?", "Flatten", views.NEGATIVE),
        dcc.Interval(id="fast", interval=LIVE_REFRESH_MS),
        dcc.Interval(id="slow", interval=SLOW_REFRESH_MS),
    ], className="page")


# ---------- The app ----------

def create_app(project: Project) -> Dash:
    control = TraderControl(project)
    app = Dash(__name__, title=TITLE, external_stylesheets=[dbc.themes.BOOTSTRAP, FONTS],
               suppress_callback_exceptions=True)
    app.layout = layout

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
                  Output("market", "children"), Output("clock-ny", "children"), Output("clock-local", "children"),
                  Output("kpis", "children"), Output("warnings", "children"),
                  Output("start", "disabled"), Output("stop", "disabled"), Output("flatten", "disabled"),
                  Input("fast", "n_intervals"))
    def refresh_header(_):
        status, trades, _, day = live_day()
        head = views.header(project, status, now())
        kpis = segments([kpi_segment(k) for k in views.live_kpis(status, trades, day) if k.title != views.TODAY_KPI])
        warnings = [html.Div(f"! {text}", className="warning-line") for text in head.warnings]
        return (head.mode.upper(), head.mode_tone, head.state.upper(), head.state_tone,
                head.market.upper(), head.new_york_time.upper(), head.local_time.upper(), kpis, warnings,
                head.running, not head.running, not head.running)

    @app.callback(Output("live-tab", "children"), Input("fast", "n_intervals"), State("tabs", "active_tab"))
    def refresh_live(_, active):
        if active != LIVE:
            return no_update
        status, trades, equity, day = live_day()
        today = next(k for k in views.live_kpis(status, trades, day) if k.title == views.TODAY_KPI)
        pnl = html.Div([html.Div(today.value, className=f"big-number {today.tone}"),
                        html.Div(today.detail, className=f"big-detail {today.tone}")])
        event_lines = [html.Div([html.Span(time, className="muted"), html.Span(stock, className="accent"),
                                 html.Span(text)], className="event") for time, stock, text in views.events(status)]
        return html.Div([
            dbc.Row([
                dbc.Col(panel(1, f"Equity {day}", chart(views.equity_figure(
                    [("Equity", equity[equity["day"] == day], views.LIVE_COLOUR)])), tall=True), lg=8),
                dbc.Col([panel(2, "P&L today", pnl),
                         panel(3, "Positions", [table(views.positions_table(status, project.risk)),
                                                html.Div(views.exposure_line(status, project.risk),
                                                         className="footnote")])], lg=4),
            ], className="g-2"),
            dbc.Row([
                dbc.Col(panel(4, "Tape", table(views.trades_table(trades, day))), lg=8),
                dbc.Col([panel(5, "Strategies", table(views.strategy_table(status))),
                         panel(6, "Events", html.Div(event_lines or html.Div("NOTHING YET", className="empty"),
                                                     className="events"))], lg=4),
            ], className="g-2"),
        ])

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

    @app.callback(Output("backtest-state", "children"), Input("run-backtest", "n_clicks"),
                  State("compare-day", "value"), prevent_initial_call=True)
    def run_backtest(_, day):
        if not day:
            return "Pick a day first"
        control.run_backtest(day)
        return f"Backtesting {day}..."

    @app.callback(Output("compare-body", "children"), Input("compare-day", "value"), Input("fast", "n_intervals"),
                  State("tabs", "active_tab"))
    def refresh_compare(day, _, active):
        if active != COMPARE:
            return no_update
        if not day:
            return html.Div("NO TRADED DAYS RECORDED YET", className="empty")
        job = control.jobs.get(day)
        result = comparison.compare(project.live_records, project.backtest_records(day), day)
        notes = []
        if job and job.state == "running":
            notes.append(html.Div(f"> Backtesting {day}...", className="warning-line"))
        elif job and job.state == "failed":
            notes.append(panel(0, "Backtest failed", html.Pre(job.output[-2000:], className="log")))
        if not result["has_backtest"]:
            notes.append(html.Div("NO BACKTEST OF THIS DAY YET: PRESS RUN BACKTEST", className="empty"))
            return html.Div(notes)
        return html.Div(notes + [
            dbc.Row([dbc.Col(panel(1, "Summary", table(views.comparison_table(result))), lg=5),
                     dbc.Col(panel(2, "Equity: live vs backtest", chart(views.comparison_figure(result))), lg=7)],
                    className="g-2"),
            panel(3, "Fills, side by side", table(views.fills_table(result))),
        ])

    # ----- History -----

    @app.callback(Output("history-body", "children"), Output("history-day-chart", "children"),
                  Output("history-day", "options"), Output("history-day", "value"),
                  Input("tabs", "active_tab"), Input("slow", "n_intervals"), Input("history-day", "value"))
    def refresh_history(active, _, chosen_day):
        if active != HISTORY:
            return no_update, no_update, no_update, no_update
        trades, equity = records.trades(project.live_records), records.equity(project.live_records)
        history = metrics.day_history(trades, equity)
        days = list(history["day"])
        day = chosen_day if chosen_day in days else (days[0] if days else None)
        intraday = equity[equity["day"] == day] if day else equity.iloc[0:0]
        body = html.Div([
            html.Div(segments([kpi_segment(k) for k in views.history_kpis(history)]), className="stat-line"),
            dbc.Row([dbc.Col(panel(1, "Daily P&L", chart(views.daily_pnl_figure(history))), lg=7),
                     dbc.Col(panel(2, "Days", table(views.history_table(history))), lg=5)], className="g-2"),
        ])
        day_chart = chart(views.equity_figure([("Equity", intraday, views.LIVE_COLOUR)]))
        return body, day_chart, [{"label": d, "value": d} for d in days], day

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
    print(f"Dashboard for {project.root} on http://{HOST}:{args.port}")
    create_app(project).run(host=HOST, port=args.port, debug=False)
