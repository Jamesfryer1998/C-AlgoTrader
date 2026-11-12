import json
import stat
import time

from dashboard import picks, records
from dashboard.tests.test_trader_control import wait_for

# A stand-in screen_app: writes progress as it goes, then picks, as screen_app --weekly does
FAKE_SCREEN_APP = """#!/usr/bin/env python3
import json, pathlib, sys, time
out = pathlib.Path("data/screen/weekly")
out.mkdir(parents=True, exist_ok=True)
started = time.time()
for done in range(3):
    (out / "progress.json").write_text(json.dumps({"state": "screening", "done": done, "total": 2,
                                                   "stock": "KO", "started": started, "updated": time.time()}))
    time.sleep(0.05)
(out / "top_picks.csv").write_text('rank,symbol,strategy,parameters,train_sharpe,test_sharpe,buy_hold_sharpe,'
                                   'test_return_on_max_position,test_closed_trades\\n'
                                   '1,KO,RSI,"{""period"":14,""stop_loss"":2}",1.5,1.2,0.4,0.03,25\\n')
(out / "top_picks_config.json").write_text('[{"ticker": "KO", "strategy": {"name": "RSI", "active": 1}}]')
(out / "screen_results.csv").write_text("symbol\\nKO\\n")
(out / "progress.json").write_text(json.dumps({"state": "finished", "done": 2, "total": 2, "stock": "",
                                               "started": started, "updated": time.time()}))
print("args", " ".join(sys.argv[1:]))
"""


def install_fake_screen(project):
    app = project.root / picks.SCREEN_APP
    app.parent.mkdir(parents=True, exist_ok=True)
    app.write_text(FAKE_SCREEN_APP)
    app.chmod(app.stat().st_mode | stat.S_IEXEC)


def test_progress_and_time_left():
    progress = picks.ScreenProgress(state=picks.SCREENING, done=25, total=100, started=1000.0)
    assert progress.fraction == 0.25
    assert progress.seconds_left(now=1000.0 + 600) == 1800          # 24 s a stock so far, 75 to go
    assert picks.ScreenProgress(state=picks.SCREENING, done=0, total=100, started=1000.0).seconds_left(2000) is None


def test_a_screen_runs_in_the_background_and_its_picks_are_saved(project):
    install_fake_screen(project)
    screen = picks.ScreenControl(project)
    assert screen.progress().state == ""                              # never run
    assert screen.run() == "Screening started"
    assert screen.run() == "A screen is already running"
    assert wait_for(lambda: not screen.running())

    assert screen.progress().state == picks.FINISHED and screen.progress().fraction == 1.0
    runs = picks.saved_runs(project)
    assert len(runs) == 1
    assert (picks.picks_root(project) / runs[0] / picks.PICKS_PORTFOLIO).exists()
    assert "args --weekly --top 5 --threads" in (picks.picks_root(project) / runs[0] / picks.LOG_FILE).read_text()
    table = picks.picks_table(project, runs[0])
    assert list(table["symbol"]) == ["KO"] and table["parameters"].iloc[0] == "period 14, stop_loss 2"


def test_a_screen_that_stopped_moving_long_ago_has_died(project):
    screen = picks.ScreenControl(project)
    screen.results.mkdir(parents=True)
    long_ago = time.time() - picks.STALE_AFTER.total_seconds() - 60
    (screen.results / picks.PROGRESS_FILE).write_text(json.dumps(
        {"state": "screening", "done": 3, "total": 100, "started": long_ago, "updated": long_ago}))
    assert screen.progress().state == picks.FAILED and not screen.running()


def test_a_missing_screen_app_is_reported(project):
    assert "Not built yet" in picks.ScreenControl(project).run()


def test_which_picks_a_day_trades_and_the_comparison(project):
    for run in ["2026-11-07", "2026-11-14"]:
        folder = picks.picks_root(project) / run
        folder.mkdir(parents=True)
        (folder / picks.PICKS_PORTFOLIO).write_text("[]")
    assert picks.picks_for_day(project, "2026-11-10") == "2026-11-07"
    assert picks.picks_for_day(project, "2026-11-14") == "2026-11-07"   # screened that day: from the next day
    assert picks.picks_for_day(project, "2026-11-16") == "2026-11-14"
    assert picks.picks_for_day(project, "2026-11-01") is None

    day = "2026-11-10"
    for folder, close in [(project.live_records, 99100), (project.backtest_records(day), 99050),
                          (picks.backtest_folder(project, day), 99300)]:
        folder.mkdir(parents=True, exist_ok=True)
        (folder / records.EQUITY_FILE).write_text(f"time,equity\n{day} 09:30:00,99000\n{day} 15:59:00,{close}\n")
    row = picks.comparison(project, [day]).iloc[0]
    assert (row["live"], row["live_backtest"], row["picks"]) == (100, 50, 300)
    assert row["picks_minus_live"] == 200 and row["picks_run"] == "2026-11-07"


def save_picks(project, run, portfolio):
    folder = picks.picks_root(project) / run
    folder.mkdir(parents=True, exist_ok=True)
    (folder / picks.PICKS_PORTFOLIO).write_text(json.dumps(portfolio))


def test_trading_a_weeks_picks_and_going_back_to_the_config(project):
    (project.root / "config" / "strategy.json").write_text(json.dumps(
        {"portfolio": [{"ticker": "SBUX", "strategy": {"name": "MACD"}}]}))
    week = [{"ticker": "KO", "strategy": {"name": "RSI", "period": 14}},
            {"ticker": "PEP", "strategy": [{"name": "SCALP"}, {"name": "VWAP"}]}]
    save_picks(project, "2026-11-07", week)
    assert picks.next_portfolio(project) == picks.CONFIG_PORTFOLIO
    assert picks.portfolio_list(project)[0]["ticker"] == "SBUX"

    message = picks.trade_picks(project, "2026-11-07")
    assert "picks from 2026-11-07" in message
    saved = json.loads(picks.chosen_portfolio_file(project).read_text())
    assert saved[picks.PORTFOLIO] == week and saved[picks.SOURCE] == "picks from 2026-11-07"
    assert picks.next_portfolio(project) == "picks from 2026-11-07"
    assert picks.portfolio_list(project) == week
    assert picks.describe_portfolio(week) == "KO RSI, PEP SCALP + VWAP"
    assert json.loads((project.root / "config" / "strategy.json").read_text())["portfolio"][0]["ticker"] == "SBUX"

    picks.trade_config(project)
    assert not picks.chosen_portfolio_file(project).exists()
    assert picks.next_portfolio(project) == picks.CONFIG_PORTFOLIO
    picks.trade_config(project)   # already back: nothing to remove, no error


def test_picks_that_cant_be_read_are_not_traded(project):
    assert "Can't read" in picks.trade_picks(project, "2026-01-01")
    save_picks(project, "2026-11-07", [])
    assert "Can't read" in picks.trade_picks(project, "2026-11-07")   # nothing to trade
    assert not picks.chosen_portfolio_file(project).exists()
