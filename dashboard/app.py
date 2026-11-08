"""The trading dashboard: a Dash web page over the C++ trader's records, with start / stop / flatten.

    python -m dashboard            then open http://127.0.0.1:8050

It only listens on this computer (127.0.0.1): the page can start live trading with your Alpaca keys.
What each panel shows comes from views.py; this file only lays it out and wires the buttons.
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
LOG_LINES = 120
TITLE = "AlgoTrader"

LIVE, COMPARE, HISTORY, CONFIG = "live", "compare", "history", "config"


# ---------- Building blocks ----------

def table(view: views.Table) -> html.Div:
    if not view.rows:
        return html.Div(view.empty, className="empty")
    head = html.Thead(html.Tr([html.Th(column) for column in view.columns]))
    body = html.Tbody([html.Tr([html.Td(text, className=tone) for text, tone in row]) for row in view.rows])
    return html.Div(html.Table([head, body], className="table table-sm table-dark table-hover data-table"),
                    className="table-wrap")


def kpi_cards(kpis: list[views.Kpi]) -> dbc.Row:
    return dbc.Row([dbc.Col(html.Div([html.Div(k.title, className="kpi-title"),
                                      html.Div(k.value, className=f"kpi-value {k.tone}"),
                                      html.Div(k.detail, className="kpi-detail")], className="kpi"),
                            xs=6, md=4, xl=2) for k in kpis], className="g-2 mb-2")


def panel(title: str, body, extra=None) -> html.Div:
    heading = html.Div([html.Span(title, className="panel-title"), extra or ""], className="panel-heading")
    return html.Div([heading, html.Div(body, className="panel-body")], className="panel")


def chart(figure: dict) -> dcc.Graph:
    return dcc.Graph(figure=figure, config={"displayModeBar": False}, className="chart")


def confirm(id_: str, title: str, body: str, button: str, colour: str) -> dbc.Modal:
    return dbc.Modal([dbc.ModalHeader(dbc.ModalTitle(title)), dbc.ModalBody(body, id=f"{id_}-body"),
                      dbc.ModalFooter([dbc.Button("Cancel", id=f"{id_}-cancel", color="secondary", size="sm"),
                                       dbc.Button(button, id=f"{id_}-confirm", color=colour, size="sm")])],
                     id=f"{id_}-modal", is_open=False, centered=True)


# ---------- Layout ----------

def layout() -> html.Div:
    header = html.Div([
        html.Div([html.Span(TITLE, className="brand"), html.Span(id="mode-badge", className="badge-pill"),
                  html.Span(id="state-badge", className="badge-pill")], className="header-left"),
        html.Div([html.Span(id="market"), html.Span(id="clock-ny", className="clock"),
                  html.Span(id="clock-local", className="clock")], className="header-middle"),
        html.Div([dbc.Button("Start", id="start", color="success", size="sm"),
                  dbc.Button("Stop", id="stop", color="secondary", size="sm"),
                  dbc.Button("Flatten & stop", id="flatten", color="danger", size="sm")], className="header-right"),
    ], className="header")

    tabs = dbc.Tabs([dbc.Tab(label="Live", tab_id=LIVE), dbc.Tab(label="Backtest vs live", tab_id=COMPARE),
                     dbc.Tab(label="History", tab_id=HISTORY), dbc.Tab(label="Config", tab_id=CONFIG)],
                    id="tabs", active_tab=LIVE, className="tabs")

    compare_controls = html.Div([
        dcc.Dropdown(id="compare-day", placeholder="Traded day", clearable=False, className="day-picker"),
        dbc.Button("Run backtest of this day", id="run-backtest", color="primary", size="sm"),
        html.Span(id="backtest-state", className="muted ms-2"),
    ], className="controls")

    return html.Div([
        header,
        html.Div(id="warnings"),
        html.Div(id="message"),
        tabs,
        html.Div(id="live-tab"),
        html.Div([compare_controls, html.Div(id="compare-body")], id="compare-tab", style={"display": "none"}),
        html.Div([dcc.Dropdown(id="history-day", placeholder="Day", clearable=False, className="day-picker"),
                  html.Div(id="history-body")], id="history-tab", style={"display": "none"}),
        html.Div(id="config-tab", style={"display": "none"}),
        confirm("start", "Start trading", "", "Start", "success"),
        confirm("flatten", "Flatten and stop",
                "Send an order closing every position at market, then stop the trader?", "Flatten & stop", "danger"),
        dcc.Interval(id="fast", interval=LIVE_REFRESH_MS),
        dcc.Interval(id="slow", interval=SLOW_REFRESH_MS),
    ], className="page")


# ---------- The app ----------

def create_app(project: Project) -> Dash:
    control = TraderControl(project)
    app = Dash(__name__, title=TITLE, external_stylesheets=[dbc.themes.CYBORG], suppress_callback_exceptions=True)
    app.layout = layout

    def now() -> datetime:
        return datetime.now(timezone.utc)

    @app.callback(Output("live-tab", "style"), Output("compare-tab", "style"), Output("history-tab", "style"),
                  Output("config-tab", "style"), Input("tabs", "active_tab"))
    def show_tab(active):
        return [{"display": "block" if tab == active else "none"} for tab in (LIVE, COMPARE, HISTORY, CONFIG)]

    @app.callback(Output("mode-badge", "children"), Output("mode-badge", "className"),
                  Output("state-badge", "children"), Output("state-badge", "className"),
                  Output("market", "children"), Output("clock-ny", "children"), Output("clock-local", "children"),
                  Output("warnings", "children"), Output("start", "disabled"), Output("stop", "disabled"),
                  Output("flatten", "disabled"), Input("fast", "n_intervals"))
    def refresh_header(_):
        status = records.status(project.live_records)
        head = views.header(project, status, now())
        warnings = [dbc.Alert(text, color="warning", className="warning-bar") for text in head.warnings]
        return (head.mode, f"badge-pill {head.mode_tone}", head.state, f"badge-pill {head.state_tone}",
                head.market, head.new_york_time, head.local_time, warnings,
                head.running, not head.running, not head.running)

    @app.callback(Output("live-tab", "children"), Input("fast", "n_intervals"), State("tabs", "active_tab"))
    def refresh_live(_, active):
        if active != LIVE:
            return no_update
        folder = project.live_records
        status, trades, equity = records.status(folder), records.trades(folder), records.equity(folder)
        day = views.today(status, trades, equity)
        risk = project.risk
        curve = equity[equity["day"] == day]
        return html.Div([
            kpi_cards(views.live_kpis(status, trades, day)),
            dbc.Row([dbc.Col(panel("Equity today", chart(views.equity_figure([("Equity", curve, views.LIVE_COLOUR)],
                                                                             f"Equity {day}")),), lg=7),
                     dbc.Col(panel("Positions", [table(views.positions_table(status, risk)),
                                                 html.Div(views.exposure_line(status, risk), className="footnote")]),
                             lg=5)], className="g-2"),
            dbc.Row([dbc.Col(panel("Strategies", table(views.strategy_table(status))), lg=7),
                     dbc.Col(panel("Events", html.Div([html.Div([html.Span(t, className="event-time"),
                                                                 html.Span(s, className="event-stock"),
                                                                 html.Span(text)], className="event")
                                                       for t, s, text in views.events(status)] or
                                                      html.Div("Nothing yet", className="empty"),
                                                      className="events")), lg=5)], className="g-2"),
            dbc.Row([dbc.Col(panel("Orders today", table(views.trades_table(trades, day))), lg=8),
                     dbc.Col(panel("Blocked and rejected", table(views.problems_table(trades, day))), lg=4)],
                    className="g-2"),
            dbc.Row([dbc.Col(panel("Speed", dbc.Row([dbc.Col(chart(views.latency_figure(trades, day)), md=6),
                                                     dbc.Col(chart(views.decision_figure(status)), md=6)])), lg=8),
                     dbc.Col(panel("Trader log", html.Pre(records.log_tail(folder, LOG_LINES) or "No output yet",
                                                          className="log")), lg=4)], className="g-2"),
        ])

    # ----- Start / stop / flatten -----

    @app.callback(Output("start-modal", "is_open"), Output("start-body", "children"),
                  Output("start-confirm", "color"),
                  Input("start", "n_clicks"), Input("start-cancel", "n_clicks"), Input("start-confirm", "n_clicks"),
                  prevent_initial_call=True)
    def toggle_start(*_):
        if ctx.triggered_id != "start":
            return False, no_update, no_update
        warning = "This account trades REAL MONEY. " if project.is_real_money else ""
        body = f"{warning}Start the trader ({project.describe_mode()}), as config/general.json says?"
        return True, body, "danger" if project.is_real_money else "success"

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
        return dbc.Alert(result, color="info", duration=4000, className="warning-bar")

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
        if active != COMPARE or not day:
            return no_update if active != COMPARE else html.Div("No traded days recorded yet", className="empty")
        job = control.jobs.get(day)
        result = comparison.compare(project.live_records, project.backtest_records(day), day)
        notes = []
        if job and job.state == "running":
            notes.append(html.Div(f"Backtesting {day}...", className="muted"))
        elif job and job.state == "failed":
            notes.append(dbc.Alert(["Backtest failed:", html.Pre(job.output[-2000:], className="log")], color="danger"))
        if not result["has_backtest"]:
            notes.append(html.Div("No backtest of this day yet: press Run backtest.", className="empty"))
            return html.Div(notes)
        return html.Div(notes + [
            dbc.Row([dbc.Col(panel("Summary", table(views.comparison_table(result))), lg=5),
                     dbc.Col(panel("Equity", chart(views.comparison_figure(result))), lg=7)], className="g-2"),
            panel("Fills, side by side", table(views.fills_table(result))),
        ])

    # ----- History -----

    @app.callback(Output("history-body", "children"), Output("history-day", "options"),
                  Output("history-day", "value"), Input("tabs", "active_tab"), Input("slow", "n_intervals"),
                  Input("history-day", "value"))
    def refresh_history(active, _, chosen_day):
        if active != HISTORY:
            return no_update, no_update, no_update
        folder = project.live_records
        trades, equity = records.trades(folder), records.equity(folder)
        history = metrics.day_history(trades, equity)
        days = list(history["day"])
        day = chosen_day if chosen_day in days else (days[0] if days else None)
        intraday = equity[equity["day"] == day] if day else equity.iloc[0:0]
        body = html.Div([
            kpi_cards(views.history_kpis(history)),
            dbc.Row([dbc.Col(panel("Equity, every day", chart(views.equity_figure(
                [("Equity", equity, views.LIVE_COLOUR)], "Account value"))), lg=7),
                     dbc.Col(panel("Daily P&L", chart(views.daily_pnl_figure(history))), lg=5)], className="g-2"),
            dbc.Row([dbc.Col(panel("Days", table(views.history_table(history))), lg=7),
                     dbc.Col(panel(f"One day: {day or ''} (pick above)", chart(views.equity_figure(
                         [("Equity", intraday, views.LIVE_COLOUR)], f"Equity {day or ''}"))), lg=5)], className="g-2"),
        ])
        return body, days, day

    # ----- Config -----

    @app.callback(Output("config-tab", "children"), Input("tabs", "active_tab"))
    def refresh_config(active):
        if active != CONFIG:
            return no_update
        return dbc.Row([dbc.Col(panel("Portfolio (config/strategy.json)", table(views.portfolio_table(project))), lg=7),
                        dbc.Col(panel("Risk (config/risk.json)", html.Pre(views.risk_text(project), className="log")),
                                lg=5)], className="g-2")

    return app


def main() -> None:
    parser = argparse.ArgumentParser(description="AlgoTrader's web dashboard (this computer only)")
    parser.add_argument("--port", type=int, default=PORT)
    args = parser.parse_args()
    project = Project.find()
    print(f"Dashboard for {project.root} on http://{HOST}:{args.port}")
    create_app(project).run(host=HOST, port=args.port, debug=False)
