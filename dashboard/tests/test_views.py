import json
from datetime import datetime, timezone

import pandas as pd

from dashboard import comparison, metrics, picks, records, report, views
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
    flat = views.positions_table(status, project.risk)
    assert flat.rows == [] and flat.empty == "" and flat.columns[0] == "Stock"   # just the headers when flat


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
    figure = views.equity_figure([("Live", equity, views.LIVE_COLOUR), ("Nothing", equity.iloc[0:0], "#fff")],
                                 BACKTEST_DAY)
    assert len(figure["data"]) == 1   # empty curves are left out
    assert figure["data"][0]["x"][0] == f"{BACKTEST_DAY} 09:30:00"
    assert figure["layout"]["paper_bgcolor"] == views.PANEL and not figure["layout"]["showlegend"]
    assert figure["layout"]["uirevision"] == BACKTEST_DAY   # keeps the viewer's zoom while it's the same day
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


def test_live_and_backtest_lines_both_start_at_zero(project, backtest_folder):
    live = project.live_records
    (live / records.EQUITY_FILE).write_text("time,equity\n"            # live began the day nearer 99k
                                            f"{BACKTEST_DAY} 09:30:00,99000\n{BACKTEST_DAY} 15:59:00,99012.5\n")
    result = comparison.compare(live, backtest_folder, BACKTEST_DAY)
    live_line, backtest_line = views.comparison_figure(result)["data"]
    assert live_line["y"] == [0.0, 12.5]
    assert backtest_line["y"][0] == 0.0 and backtest_line["y"][-1] == round(
        result["backtest_equity"]["equity"].iloc[-1] - result["backtest_equity"]["equity"].iloc[0], 2)


def test_history_views(tmp_path):
    (tmp_path / records.EQUITY_FILE).write_text("time,equity\n2026-10-28 15:59:00,100500\n2026-10-29 15:59:00,100250\n"
                                                "2026-10-30 15:59:00,100900\n")
    history = metrics.day_history(records.trades(tmp_path), records.equity(tmp_path))
    kpis = {k.title: k for k in views.history_kpis(history)}
    assert kpis["Total P&L"].value == "+$400.00"   # -250 then +650 (the first day has no previous close)
    assert kpis["Best day"].detail == "2026-10-30" and kpis["Worst day"].detail == "2026-10-29"
    assert kpis["Best day"].tone == views.POSITIVE
    assert texts(views.history_table(history))[0][0] == "2026-10-30"
    figure = views.daily_pnl_figure(history)
    assert figure["data"][0]["x"] == ["2026-10-28", "2026-10-29", "2026-10-30"]   # oldest first on the chart


def test_the_best_day_of_only_losing_days_is_not_green(tmp_path):
    (tmp_path / records.EQUITY_FILE).write_text("time,equity\n2026-11-09 09:30:00,100000\n2026-11-09 15:59:00,99917\n"
                                                "2026-11-10 09:30:00,99917\n2026-11-10 15:59:00,99782.49\n")
    kpis = {k.title: k for k in views.history_kpis(metrics.day_history(records.trades(tmp_path),
                                                                       records.equity(tmp_path)))}
    assert kpis["Best day"].value == "-$83.00" and kpis["Best day"].tone == views.NEGATIVE


def test_strategy_views(tmp_path):
    write_trades(tmp_path, [(f"{BACKTEST_DAY} 10:00:00", "KO", "buy", 2, 100, "sent", 0, "", "RSI"),
                            (f"{BACKTEST_DAY} 10:05:00", "KO", "sell", 2, 110, "sent", 0, "", "RSI"),
                            (f"{BACKTEST_DAY} 10:10:00", "PEP", "buy", 1, 50, "blocked", 0, "", "MACD")])
    trades = records.trades(tmp_path)
    rows = texts(views.strategies_table(metrics.strategy_performance(trades)))
    assert rows[0] == ["RSI", "KO", "2", "0", "1", "100%", "+$20.00", "+$20.00", "+$20.00", "+$20.00"]
    assert rows[1][:2] == ["MACD", "PEP"] and rows[1][6:] == ["+$0.00", "-", "-", "-"]   # blocked, never traded
    assert texts(views.strategy_stock_table(metrics.strategy_by_stock(trades))) == [
        ["RSI", "KO", "1", "100%", "+$20.00", "+$20.00"]]
    figure = views.strategy_pnl_figure(metrics.closed_trades(trades))
    assert [trace["name"] for trace in figure["data"]] == ["RSI"] and figure["data"][0]["y"] == [20.0]
    json.dumps(figure)


def test_config_views(project):
    (project.root / "config" / "strategy.json").write_text(json.dumps(
        {"portfolio": [{"ticker": "KO", "strategy": {"name": "RSI", "active": 1, "period": 14}}]}))
    assert texts(views.portfolio_table(project)) == [["KO", "RSI", "period 14"]]
    assert "KELLY" in views.risk_text(project)


def report_days(count: int):
    """`count` trading days of an equity curve that wobbles up."""
    closes, value = {}, 100000.0
    for i in range(count):
        day = (pd.Timestamp("2026-09-01") + pd.offsets.BDay(i)).strftime("%Y-%m-%d")
        start = value
        value *= 1 + (0.004 if i % 3 else -0.006)
        closes[day] = [start, value]
    rows = [{"time": f"{day} 09:{30 + j:02d}:00", "equity": v, "day": day}
            for day, values in closes.items() for j, v in enumerate(values)]
    return report.daily(pd.DataFrame(rows))


def test_report_charts():
    days = report_days(60)
    figures = [views.growth_figure(days, "k"), views.drawdown_figure(days, "k"),
               views.monthly_heatmap(report.monthly_returns(days), "k"), views.daily_returns_figure(days, "k"),
               views.distribution_figure(days, "k"), views.qq_figure(days, "k"),
               views.rolling_figure(report.rolling(days), days, "k")]
    for figure in figures:
        json.dumps(figure)                                    # plain data Dash can send
        assert figure["data"] and figure["layout"]["uirevision"] == "k"
    heatmap = figures[2]["data"][0]
    assert heatmap["y"] == ["2026"] and heatmap["zmid"] == 0
    assert heatmap["text"][0][8].startswith(("+", "-"))       # September's cell carries its signed return
    bars = figures[3]["data"][0]
    assert {colour for colour in bars["marker"]["color"]} == {views.UP_COLOUR, views.DOWN_COLOUR}
    assert [trace["name"] for trace in figures[6]["data"]] == ["20-day return", "20-day volatility (annualised)"]


def test_report_charts_say_when_there_are_too_few_days():
    days = report_days(3)
    assert views.growth_figure(days, "k")["data"]               # a curve from day one
    for figure in [views.distribution_figure(days, "k"), views.qq_figure(days, "k"),
                   views.rolling_figure(report.rolling(days), days, "k")]:
        assert figure["data"] == [] and "NEEDS AT LEAST" in figure["layout"]["annotations"][0]["text"]
    assert views.growth_figure(report.daily(pd.DataFrame(columns=["time", "equity", "day"])), "k")["data"] == []
    kpis = {k.title: k for k in views.report_kpis(report.summary(days))}
    assert kpis["Days"].value == "3" and kpis["Sharpe"].value == "-"      # too short to annualise


def test_strategy_lines_never_reuse_a_colour():
    names = [f"S{i}" for i in range(len(views.STRATEGY_COLOURS) + 2)]
    closed = pd.DataFrame({"strategy": names, "time": [f"{BACKTEST_DAY} 10:0{i}:00" for i in range(len(names))],
                           "pnl": [1.0] * len(names)})
    lines = views.strategy_pnl_figure(closed)["data"]
    assert [line["name"] for line in lines] == names[:len(views.STRATEGY_COLOURS)] + [views.OTHER_STRATEGIES]
    assert len({line["line"]["color"] for line in lines}) == len(lines)
    assert lines[-1]["y"] == [1.0, 2.0]                                   # the two extra strategies, added up


def test_screen_status_line():
    screening = picks.ScreenProgress(state=picks.SCREENING, done=25, total=100, stock="KO", started=1000.0)
    text, fraction = views.screen_status(screening, ["2026-11-07"], now=1600.0)
    assert text == "Screening 25 of 100 stocks on KO, about 30m left" and fraction == 0.25
    text, _ = views.screen_status(picks.ScreenProgress(state=picks.FINISHED), ["2026-11-14", "2026-11-07"], 0)
    assert text.startswith("Last screened 2026-11-14")
    text, fraction = views.screen_status(picks.ScreenProgress(state=picks.FAILED), [], 0)
    assert "failed" in text and fraction == 0.0


def test_picks_against_live_views():
    days = pd.DataFrame({"day": ["2026-11-09", "2026-11-10"], "live": [-80.0, 100.0], "live_backtest": [-60.0, None],
                         "picks": [40.0, 50.0], "picks_run": ["2026-11-07", "2026-11-07"]})
    days["picks_minus_live"] = days["picks"] - days["live"]
    kpis = {k.title: k for k in views.picks_kpis(days)}
    assert kpis["Live"].value == "+$20.00" and kpis["Picks"].value == "+$90.00"
    assert kpis["Picks - live"].value == "+$70.00" and kpis["Picks ahead"].value == "1 of 2 days"

    figure = views.picks_figure(days)
    assert [line["name"] for line in figure["data"]] == ["Live", "Live's portfolio backtested", "Picks backtested"]
    assert figure["data"][0]["y"] == [-80.0, 20.0]                    # added up day by day
    assert figure["data"][1]["x"] == ["2026-11-09"]                   # a day without that backtest is left out
    json.dumps(figure)
    rows = texts(views.picks_days_table(days))
    assert rows[0] == ["2026-11-10", "+$100.00", "-", "+$50.00", "-$50.00", "2026-11-07"]   # newest first
    assert views.weekly_picks_table(pd.DataFrame()).rows == []
