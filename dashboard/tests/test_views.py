import json
from datetime import datetime, timezone

import pandas as pd

from dashboard import comparison, metrics, records, views
from dashboard.tests.conftest import BACKTEST_DAY
from dashboard.tests.test_records_and_metrics import write_trades

NOW = datetime(2026, 10, 29, 18, 0, tzinfo=timezone.utc)


def texts(table):
    return [[text for text, _ in row] for row in table.rows]


def tones(table):
    return [[tone for _, tone in row] for row in table.rows]


def test_formatting():
    assert views.money(1234.5) == "$1,234.50"
    assert views.money(-0.001) == "$0.00"                 # never "-$0.00"
    assert views.signed_money(-1234.5) == "-$1,234.50"
    assert views.signed_money(0.004) == "+$0.00"
    assert views.percent(-0.0001, signed=True) == "+0.00%"   # never "+-0.00%"
    assert views.percent(None) == "-"
    assert views.number(10.0, 0) == "10" and views.number(3.14159) == "3.14"
    assert views.milliseconds(12.34) == "12.3 ms" and views.milliseconds(0.0042) == "4 µs"
    assert views.text_or(float("nan"), "-") == "-" and views.text_or("note") == "note"
    assert views.tone_of(-1) == views.NEGATIVE and views.tone_of(0) == views.PLAIN


def test_header_for_a_running_paper_trader(project):
    status = {"state": "running", "updated_utc": "2026-10-29T17:59:59Z",
              "last_bar_received_utc": "2026-10-29T17:59:30Z", "connected": True}
    header = views.header(project, status, NOW)
    assert header.state == "Running" and header.state_tone == views.POSITIVE
    assert header.mode == "Paper account" and header.mode_tone == views.POSITIVE
    assert header.market == "Open, closes in 2h 0m (regular hours)"
    assert header.running and header.warnings == []


def test_header_when_real_money_and_not_running(project):
    general = project.general
    general["alpaca"]["trading_url"] = "https://api.alpaca.markets"
    (project.root / "config" / "general.json").write_text(json.dumps(general))
    header = views.header(project, {}, NOW)
    assert header.mode == "REAL MONEY" and header.mode_tone == views.NEGATIVE
    assert header.state == "Not running" and not header.running


def test_live_tab_from_a_real_backtest(project, backtest_folder):
    status = records.status(backtest_folder)
    trades = records.trades(backtest_folder)
    day = views.today(status, trades, records.equity(backtest_folder))
    assert day == BACKTEST_DAY

    kpis = {k.title: k for k in views.live_kpis(status, trades, day)}
    assert kpis["Equity"].value == "$100,008.95"
    assert kpis["Orders"].value == str(int((trades["status"] == "sent").sum()))
    assert kpis["Positions"].value == "0"

    strategies = views.strategy_table(status)
    assert [row[0] for row in texts(strategies) if row[0]] == ["AAPL", "KO", "PEP"]
    tape = views.trades_table(trades, day)
    newest = trades.iloc[-1]
    assert tape.rows[0][0][0] == newest["time"][11:19]   # newest first
    assert tape.rows[0][1] == ((views.BUY_ARROW, views.POSITIVE) if newest["side"] == "buy"
                               else (views.SELL_ARROW, views.NEGATIVE))
    assert "nan" not in [text for row in texts(tape) for text in row]   # empty notes stay empty
    assert views.positions_table(status, project.risk).rows == []


def test_positions_flag_a_stock_over_its_cap(project):
    status = {"account": {"equity": 10000, "available": True},
              "stocks": [{"ticker": "KO", "price": 60, "position": {"quantity": 50, "avg_price": 58,
                                                                     "market_value": 3000, "unrealised_pnl": 100}}]}
    table = views.positions_table(status, project.risk)
    assert texts(table)[0] == ["KO", "50", "58", "60", "+$100.00", "30.0%"]
    assert tones(table)[0][4] == views.POSITIVE and tones(table)[0][5] == views.WARNING   # 30% > the 20% cap


def test_events_newest_first():
    status = {"events": [{"time": f"{BACKTEST_DAY} 10:00:00", "ticker": "KO", "text": "first"},
                         {"time": f"{BACKTEST_DAY} 10:05:00", "ticker": "PEP", "text": "second"}]}
    assert views.events(status) == [("10:05", "PEP", "second"), ("10:00", "KO", "first")]


def test_strategy_levels_on_one_line():
    status = {"stocks": [{"ticker": "KO", "strategies": [{"label": "RSI", "lower": 30, "current": 45.2, "upper": 70,
                                                          "state": "hold", "ready": True}]}]}
    assert texts(views.strategy_table(status)) == [["KO", "RSI", "30 < 45.2 < 70", "hold"]]


def test_charts_are_plotly_figures(backtest_folder):
    equity = records.equity(backtest_folder)
    figure = views.equity_figure([("Live", equity, views.LIVE_COLOUR), ("Nothing", equity.iloc[0:0], "#fff")])
    assert len(figure["data"]) == 1   # empty curves are left out
    assert figure["data"][0]["x"][0] == f"{BACKTEST_DAY} 09:30:00"
    assert figure["layout"]["paper_bgcolor"] == views.PANEL and not figure["layout"]["showlegend"]
    json.dumps(figure)   # plain data: Dash can send it as it is


def test_comparison_views(project, backtest_folder):
    live = project.live_records
    for name in [records.TRADES_FILE, records.EQUITY_FILE]:
        (live / name).write_text((backtest_folder / name).read_text())
    result = comparison.compare(live, backtest_folder, BACKTEST_DAY)
    summary = texts(views.comparison_table(result))
    assert summary[0][0] == "P&L" and summary[0][1] == summary[0][2] and summary[0][3] == "+$0.00"
    assert all(row[-1][0] == comparison.MATCH for row in views.fills_table(result).rows)
    figure = views.comparison_figure(result)
    assert [trace["name"] for trace in figure["data"]] == ["Live", "Backtest"] and figure["layout"]["showlegend"]


def test_history_views(tmp_path):
    (tmp_path / records.EQUITY_FILE).write_text("time,equity\n2026-10-28 15:59:00,100500\n2026-10-29 15:59:00,100250\n"
                                                "2026-10-30 15:59:00,100900\n")
    history = metrics.day_history(records.trades(tmp_path), records.equity(tmp_path))
    kpis = {k.title: k for k in views.history_kpis(history)}
    assert kpis["Total P&L"].value == "+$400.00"   # -250 then +650 (the first day has no previous close)
    assert kpis["Best day"].detail == "2026-10-30" and kpis["Worst day"].detail == "2026-10-29"
    assert texts(views.history_table(history))[0][0] == "2026-10-30"
    figure = views.daily_pnl_figure(history)
    assert figure["data"][0]["x"] == ["2026-10-28", "2026-10-29", "2026-10-30"]   # oldest first on the chart


def test_config_views(project):
    (project.root / "config" / "strategy.json").write_text(json.dumps(
        {"portfolio": [{"ticker": "KO", "strategy": {"name": "RSI", "active": 1, "period": 14}}]}))
    assert texts(views.portfolio_table(project)) == [["KO", "RSI", "period 14"]]
    assert "KELLY" in views.risk_text(project)
