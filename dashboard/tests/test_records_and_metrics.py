from datetime import datetime, timezone

import pandas as pd

from dashboard import comparison, metrics, records
from dashboard.project import Project
from dashboard.tests.conftest import BACKTEST_DAY

NOW = datetime(2026, 10, 29, 18, 0, tzinfo=timezone.utc)   # 14:00 New York, a Thursday


def write_trades(folder, rows):
    """trades.csv from (time, ticker, side, quantity, price, status[, round_trip_ms, note, strategy]) tuples."""
    lines = [",".join(records.TRADE_COLUMNS)]
    for i, row in enumerate(rows, 1):
        time, ticker, side, quantity, price, status, *rest = row
        ms = rest[0] if rest else 0
        note = rest[1] if len(rest) > 1 else ""
        strategy = rest[2] if len(rest) > 2 else "RSI"
        lines.append(f"{i},{time},,{strategy},{ticker},{side},{side.upper()},simple,{quantity},{price},0,0,{status},,,,"
                     f"{ms},{note}")
    (folder / records.TRADES_FILE).write_text("\n".join(lines) + "\n")


# ---------- Reading what the C++ trader wrote ----------

def test_reads_a_real_backtests_records(backtest_folder):
    status = records.status(backtest_folder)
    assert status["state"] == "stopped"
    assert {stock["ticker"] for stock in status["stocks"]} == {"KO", "PEP", "AAPL"}

    trades = records.trades(backtest_folder)
    assert len(trades) == 33
    assert set(trades["day"]) == {BACKTEST_DAY}
    assert trades["price"].dtype.kind == "f"

    equity = records.equity(backtest_folder)
    assert len(equity) == 390
    assert equity["time"].iloc[0] == f"{BACKTEST_DAY} 09:30:00"
    assert records.positions(backtest_folder).empty   # flat at the end


def test_missing_or_half_written_files_read_as_nothing(tmp_path):
    (tmp_path / records.STATUS_FILE).write_text('{"state": "runn')   # mid-write
    assert records.status(tmp_path) == {}
    assert records.trades(tmp_path).empty
    assert records.equity(tmp_path).empty
    assert records.log_tail(tmp_path, 10) == ""


def test_log_tail_keeps_the_last_lines(tmp_path):
    (tmp_path / records.LOG_FILE).write_text("\n".join(f"line {i}" for i in range(500)))
    assert records.log_tail(tmp_path, 3).splitlines() == ["line 497", "line 498", "line 499"]


# ---------- Health and market hours ----------

def status_at(state="running", heartbeat_age=1, bar_age=30, connected=True):
    def ago(seconds):
        return (NOW - pd.Timedelta(seconds=seconds)).strftime("%Y-%m-%dT%H:%M:%SZ")
    return {"state": state, "updated_utc": ago(heartbeat_age), "last_bar_received_utc": ago(bar_age),
            "connected": connected}


def test_a_running_trader_is_healthy():
    health = metrics.health(status_at(), NOW, market_open=True)
    assert health.running and health.warnings == []


def test_health_warnings():
    assert "crashed" in metrics.health(status_at(heartbeat_age=60), NOW, True).warnings[0]
    assert "disconnected" in metrics.health(status_at(connected=False), NOW, True).warnings[0]
    assert "stale" in metrics.health(status_at(bar_age=600), NOW, market_open=True).warnings[0]
    assert metrics.health(status_at(bar_age=600), NOW, market_open=False).warnings == []   # no bars when closed
    assert not metrics.health({}, NOW, True).running


def test_market_hours_from_alpacas_clock():
    status = {"market_clock": {"known": True, "is_open": True, "next_open_utc": "2026-10-30T13:30:00Z",
                               "next_close_utc": "2026-10-29T20:00:00Z"}}
    hours = metrics.market_hours(status, NOW)
    assert hours.is_open and hours.exact
    assert hours.label == "Open, closes in 2h 0m"


def test_market_hours_without_a_clock_are_regular_hours():
    assert metrics.market_hours({}, NOW).label == "Open, closes in 2h 0m"
    saturday = datetime(2026, 10, 31, 15, 0, tzinfo=timezone.utc)
    closed = metrics.market_hours({}, saturday)
    assert not closed.is_open
    assert closed.label == "Closed, opens in 1d 23h 30m"   # Monday 09:30 New York


def test_durations():
    assert metrics.describe_duration(40) == "40s"
    assert metrics.describe_duration(3725) == "1h 2m"
    assert metrics.describe_duration(95000) == "1d 2h 23m"


# ---------- Orders, exposure, closed trades, days ----------

def test_latency_of_orders_sent(tmp_path):
    write_trades(tmp_path, [(f"{BACKTEST_DAY} 10:0{i}:00", "KO", "buy", 1, 60, "sent", ms)
                            for i, ms in enumerate([100, 200, 300, 400, 1000])]
                 + [(f"{BACKTEST_DAY} 11:00:00", "KO", "buy", 1, 60, "blocked", 0)])
    figures = metrics.latency(records.trades(tmp_path))
    assert figures["orders"] == 5
    assert figures["median"] == 300
    assert figures["max"] == 1000


def test_exposure_against_the_caps(project):
    status = {"account": {"equity": 100000},
              "stocks": [{"ticker": "KO", "price": 60, "position": {"quantity": 100, "market_value": 6000}},
                         {"ticker": "PEP", "price": 150, "position": {"quantity": -20, "market_value": -3000}},
                         {"ticker": "WMT", "price": 120, "position": {"quantity": 0}}]}
    frame, summary = metrics.exposure(status, project.risk)
    assert list(frame["ticker"]) == ["KO", "PEP"]   # flat stocks left out
    assert list(frame["percent_of_equity"]) == [6.0, 3.0]
    assert list(frame["cap_percent"]) == [20.0, 20.0]
    assert summary["gross_percent"] == 9.0
    assert summary["engine"] == "KELLY" and summary["share_limit"] is None


def test_closed_trades_long_short_and_flip(tmp_path):
    day = BACKTEST_DAY
    write_trades(tmp_path, [(f"{day} 10:00:00", "KO", "buy", 2, 100, "sent"),
                            (f"{day} 10:01:00", "KO", "buy", 2, 110, "sent"),       # average 105
                            (f"{day} 10:02:00", "KO", "sell", 6, 120, "sent"),      # close 4 (+60), short 2 at 120
                            (f"{day} 10:03:00", "KO", "buy", 2, 100, "adjustment"),  # broker stop: +40
                            (f"{day} 10:04:00", "KO", "buy", 9, 100, "blocked")])    # never happened
    closed = metrics.closed_trades(records.trades(tmp_path))
    assert list(closed["pnl"]) == [60.0, 40.0]
    assert list(closed["entry"]) == [105.0, 120.0]


def test_each_strategy_owns_the_trades_it_opened(tmp_path):
    day = BACKTEST_DAY
    write_trades(tmp_path, [(f"{day} 10:00:00", "KO", "buy", 2, 100, "sent", 0, "", "RSI"),
                            (f"{day} 10:05:00", "KO", "sell", 2, 110, "adjustment", 0, "", "broker"),  # take profit: +20
                            (f"{day} 10:10:00", "PEP", "buy", 1, 50, "sent", 0, "", "MACD"),
                            (f"{day} 10:15:00", "PEP", "sell", 1, 45, "sent", 0, "", "MACD"),           # -5
                            (f"{day} 10:20:00", "KO", "buy", 1, 100, "sent", 0, "", "MACD"),
                            (f"{day} 10:25:00", "KO", "sell", 1, 101, "sent", 0, "", "RSI"),            # MACD's: +1
                            (f"{day} 10:30:00", "KO", "buy", 9, 100, "blocked", 0, "", "RSI")])
    trades = records.trades(tmp_path)
    assert list(metrics.closed_trades(trades)["strategy"]) == ["RSI", "MACD", "MACD"]

    performance = metrics.strategy_performance(trades)
    assert list(performance["strategy"]) == ["RSI", "MACD"]          # best first; "broker" isn't a strategy
    rsi, macd = performance.iloc[0], performance.iloc[1]
    assert rsi["pnl"] == 20 and rsi["closed_trades"] == 1 and rsi["win_rate_percent"] == 100
    assert rsi["orders_sent"] == 2 and rsi["blocked"] == 1 and rsi["stocks"] == "KO"
    assert macd["pnl"] == -4 and macd["closed_trades"] == 2 and macd["win_rate_percent"] == 50
    assert macd["best"] == 1 and macd["worst"] == -5 and macd["stocks"] == "KO, PEP"

    by_stock = metrics.strategy_by_stock(trades)
    assert [tuple(row) for row in by_stock[["strategy", "ticker", "pnl"]].values] == [
        ("RSI", "KO", 20.0), ("MACD", "KO", 1.0), ("MACD", "PEP", -5.0)]


def test_day_history(tmp_path):
    (tmp_path / records.EQUITY_FILE).write_text("time,equity\n"
                                                "2026-10-28 10:00:00,100000\n2026-10-28 15:59:00,100500\n"
                                                "2026-10-29 10:00:00,101000\n2026-10-29 12:00:00,99990\n"
                                                "2026-10-29 15:59:00,100250\n")
    write_trades(tmp_path, [("2026-10-29 10:00:00", "KO", "buy", 1, 100, "sent"),
                            ("2026-10-29 11:00:00", "KO", "sell", 1, 90, "sent"),
                            ("2026-10-29 11:30:00", "KO", "buy", 1, 100, "blocked")])
    history = metrics.day_history(records.trades(tmp_path), records.equity(tmp_path))
    assert list(history["day"]) == ["2026-10-29", "2026-10-28"]   # newest first
    latest = history.iloc[0]
    assert latest["start_equity"] == 100500            # the previous day's close
    assert latest["pnl"] == -250
    assert round(latest["max_drawdown_percent"], 2) == 1.0   # 101000 down to 99990
    assert latest["orders_sent"] == 2 and latest["blocked"] == 1
    assert latest["closed_trades"] == 1 and latest["win_rate_percent"] == 0
    assert history.iloc[1]["pnl"] == 500


# ---------- Backtest vs live ----------

def test_a_day_compared_with_itself_matches_every_fill(project, backtest_folder):
    # The backtest's records standing in for a live day that went exactly to plan
    live = project.live_records
    for name in [records.TRADES_FILE, records.EQUITY_FILE]:
        (live / name).write_text((backtest_folder / name).read_text())

    result = comparison.compare(live, backtest_folder, BACKTEST_DAY)
    assert result["has_backtest"]
    assert result["live_only"] == 0 and result["backtest_only"] == 0
    assert result["matched"] == len(records.trades(live)[records.trades(live)["status"].isin(records.CHANGED_POSITION)])
    assert result["live"]["pnl"] == result["backtest"]["pnl"]
    assert (result["fills"]["price_difference"] == 0).all()
    assert comparison.traded_days(live) == [BACKTEST_DAY]


def test_fills_on_one_side_only(tmp_path):
    live, test = tmp_path / "live", tmp_path / "test"
    live.mkdir()
    test.mkdir()
    write_trades(live, [(f"{BACKTEST_DAY} 10:00:00", "KO", "buy", 1, 60.05, "sent"),
                        (f"{BACKTEST_DAY} 11:00:00", "PEP", "buy", 1, 150, "sent")])
    write_trades(test, [(f"{BACKTEST_DAY} 10:00:00", "KO", "buy", 1, 60.00, "sent"),
                        (f"{BACKTEST_DAY} 12:00:00", "WMT", "sell", 1, 120, "sent")])
    fills = comparison.match_fills(records.trades(live), records.trades(test))
    assert list(fills["result"]) == [comparison.MATCH, comparison.LIVE_ONLY, comparison.BACKTEST_ONLY]
    assert round(fills.loc[0, "price_difference"], 2) == 0.05


# ---------- The project ----------

def test_where_the_records_are(project):
    assert project.live_records == project.root / "data" / "live"
    assert project.describe_mode() == "Paper account" and not project.is_real_money
    general = project.general
    general["alpaca"]["dry_run"] = True
    (project.root / "config" / "general.json").write_text(__import__("json").dumps(general))
    assert project.live_records.name == "live_dry_run"
    assert Project.find(project.root / "data" / "live") == project
