"""The Research tab: running morning_app and showing its report."""

import json
import time

from dashboard import research, views
from dashboard.project import Project


def sample_report() -> dict:
    """A report.json as morning_app writes it (MorningReport::toJson), cut down."""
    def curve(variant, selection, values):
        days = ["2024-10-30", "2024-10-31", "2024-11-01"]
        return {"variant": variant, "selection": selection, "points": [[d, v] for d, v in zip(days, values)]}

    def row(variant, selection, period, sharpe, trades=10, deflated=None):
        return {"variant": variant, "selection": selection, "period": period, "days": 3, "trades": trades,
                "win_rate": 0.4, "total_return": 0.02, "cagr": 0.1, "sharpe": sharpe, "max_drawdown": 0.01,
                "avg_net_per_trade": 5.0, "avg_cost_per_trade": 1.5, "gross_to_cost": 4.3,
                "deflated_sharpe": deflated}

    return {
        "generated_utc": "2026-11-14T18:05:00Z",
        "dates": {"start": "2023-11-01", "development_end": "2024-10-31", "validation_end": "2025-10-31",
                  "end": "2026-11-13"},
        "holdout_opened": None, "holdout_traded": False, "universe_symbols": 11234, "days_traded": 500,
        "downloads": 4200, "stocks_a_day": 20, "round_trip_cost_percent": 0.06,
        "step1": {"moves": {"in play": {"stock_days": 9000, "move_10_minutes_percent": 0.42,
                                        "move_30_minutes_percent": 0.8, "atr_percent": 4.1, "relative_volume": 3.2},
                            "random": {"stock_days": 9000, "move_10_minutes_percent": 0.12,
                                       "move_30_minutes_percent": 0.22, "atr_percent": 2.0, "relative_volume": 1.0}},
                  "move_to_cost": 7.0},
        "results": [row("ORB", "random", "validation", 0.2),
                    row("ORB", "in play", "validation", 1.6, deflated=0.97),
                    row("ORB", "in play", "development", 2.1, deflated=0.99),
                    row("SPY buy and hold", "SPY", "validation", 1.1, trades=0)],
        "gates": [{"step": "Step 1: moves against costs", "variant": "", "period": "development + validation",
                   "pass": True, "checks": [{"name": "In-play move at least 3x", "pass": True, "detail": "7.0x"}]},
                  {"step": "Step 3: the breakout as published", "variant": "ORB", "period": "validation",
                   "pass": False, "checks": [{"name": "Sharpe at least 1.0", "pass": True, "detail": "1.60"},
                                             {"name": "Ahead of random", "pass": False, "detail": "0.30"}]}],
        "curves": [curve("ORB", "in play", [1.0, 1.01, 1.03]), curve("ORB", "random", [1.0, 0.99, 1.0]),
                   curve("ORB, 10-minute hold", "in play", [1.0, 1.0, 1.01]),
                   curve("SPY buy and hold", "SPY", [1.0, 1.002, 1.004])],
    }


def save(project: Project, name: str, contents: dict) -> None:
    folder = research.folder(project)
    folder.mkdir(parents=True, exist_ok=True)
    (folder / name).write_text(json.dumps(contents))


def test_no_report_yet(project):
    assert research.load_report(project) is None
    text, fraction = views.research_status(research.ResearchProgress(), None, time.time())
    assert text.startswith("Not run yet") and fraction == 0
    assert views.research_variants(None) == []
    assert views.research_figure(None, None)["data"] == []


def test_progress_while_running_and_a_dead_run(project):
    control = research.ResearchControl(project)
    now = time.time()
    save(project, research.PROGRESS_FILE, {"state": "running", "stage": "Picking and trading each day",
                                           "done": 100, "total": 800, "started": now - 600, "updated": now})
    first = control.progress(now)
    assert first.state == research.RUNNING and control.running()
    assert first.seconds_left(now) is None   # not moved since the dashboard first saw it

    save(project, research.PROGRESS_FILE, {"state": "running", "stage": "Picking and trading each day",
                                           "done": 200, "total": 800, "started": now - 600, "updated": now + 60})
    later = control.progress(now + 60)
    assert later.seconds_left(now + 60) == 60 / 100 * 600   # 100 days a minute, 600 to go
    text, fraction = views.research_status(later, None, now + 60)
    assert text.startswith("Picking and trading each day: 200 of 800") and "about 6m left" in text
    assert fraction == 0.25

    gone_quiet = later.updated + research.STALE_AFTER.total_seconds() + 1
    assert control.progress(gone_quiet).state == research.FAILED


def test_running_without_the_app_built(project):
    assert research.ResearchControl(project).run().startswith("Not built yet")


def test_the_report_as_tables(project):
    save(project, research.REPORT_FILE, sample_report())
    report_json = research.load_report(project)
    kpis = {kpi.title: kpi.value for kpi in views.research_kpis(report_json)}
    assert kpis["Holdout"] == "still sealed" and kpis["Days traded"] == "500"

    gates = views.gates_table(report_json)
    assert [row[3][0] for row in gates.rows] == ["PASS", "FAIL"]
    assert "no: Ahead of random" in gates.rows[1][4][0]          # a failed gate shows what failed
    assert "Sharpe at least" not in gates.rows[1][4][0]

    moves = views.moves_table(report_json)
    assert moves.rows[0][0][0] == "in play" and moves.rows[0][-1][0] == "7.0x"

    results = views.research_results_table(report_json)
    order = [(row[0][0], row[1][0], row[2][0]) for row in results.rows]
    assert order == [("development", "ORB", "in play"), ("validation", "ORB", "in play"),
                     ("validation", "ORB", "random"), ("validation", "SPY buy and hold", "SPY")]
    assert results.rows[0][-1][0] == "0.99" and results.rows[2][-1][0] == views.NOTHING
    assert results.rows[3][7][0] == views.NOTHING   # SPY has no trades


def test_the_chart_compares_in_play_random_and_spy(project):
    report_json = sample_report()
    assert views.research_variants(report_json) == ["ORB", "ORB, 10-minute hold"]
    figure = views.research_figure(report_json, "ORB")
    names = [line["name"] for line in figure["data"]]
    assert names == ["ORB, in play", "ORB, random", "SPY buy and hold"]
    assert figure["data"][0]["y"] == [0.0, 0.01, 0.03]
    assert [shape["x0"] for shape in figure["layout"]["shapes"]] == ["2024-10-31", "2025-10-31"]


def test_each_study_has_its_own_folder_and_clearing_keeps_the_data(project):
    pairs = research.STUDIES["pairs"]
    assert research.study_of("pairs") is pairs and research.study_of(None) is research.MORNING
    save(project, research.REPORT_FILE, sample_report())
    pairs_folder = research.folder(project, pairs)
    pairs_folder.mkdir(parents=True)
    (pairs_folder / research.REPORT_FILE).write_text(json.dumps({**sample_report(), "chosen": "matched"}))
    (pairs_folder / pairs.log).write_text("log")
    (pairs_folder / "holdout_opened.json").write_text("{}")
    (pairs_folder / "daily").mkdir()
    (pairs_folder / "daily" / "AAPL.csv").write_text("bars")
    assert research.load_report(project, pairs)["chosen"] == "matched"

    controls = {key: research.ResearchControl(project, study) for key, study in research.STUDIES.items()}
    assert research.clear_results(project, controls) == "Cleared 3 result files"
    assert research.load_report(project) is None and research.load_report(project, pairs) is None
    assert (pairs_folder / "holdout_opened.json").exists() and (pairs_folder / "daily" / "AAPL.csv").exists()
    assert research.clear_results(project, controls) == "Nothing to clear"

    save(project, research.PROGRESS_FILE, {"state": "running", "stage": "x", "done": 1, "total": 2,
                                           "updated": time.time()})
    assert research.clear_results(project, controls) == "Not while research is running"


def test_another_studys_report_names_its_own_selection():
    report_json = {**sample_report(), "chosen": "matched", "count_label": "pairs a month"}
    report_json["curves"] = [{**curve, "selection": "matched" if curve["selection"] == "in play" else curve["selection"]}
                             for curve in report_json["curves"]]
    names = [line["name"] for line in views.research_figure(report_json, "ORB")["data"]]
    assert names == ["ORB, matched", "ORB, random", "SPY buy and hold"]
    kpis = {kpi.title: kpi.value for kpi in views.research_kpis(report_json)}
    assert kpis["Pairs a month"] == "20"
    assert views.moves_table({**report_json, "step1": {"moves": {}, "move_to_cost": 0}}).rows == []


def test_the_timing_search_shows_its_leaderboard():
    measures = {"cagr": 0.08, "sharpe": 1.1, "max_drawdown": 0.1, "exposure": 0.6, "trades": 30, "mix_cagr": 0.06,
                "edge": 0.02, "capture": 0.7, "days": 1000}
    report_json = {**sample_report(), "leaderboard": {
        "rules_tried": 4238, "eligible": 1061, "symbol": "SPY", "cash": "BIL",
        "ahead_of_mix": {"development": 0.34, "validation": 0.33}, "edge_rank_correlation": -0.42,
        "families": [{"family": "turn_of_month", "tried": 49, "eligible": 42, "best_development_edge": 0.07,
                      "median_development_edge": 0.017, "median_validation_edge": -0.04, "validation_ahead": 0.24}],
        "top": [{"rank": 1, "rule": "momentum: days 10", "family": "momentum", "finalist": True,
                 "development": measures, "validation": {**measures, "edge": -0.039},
                 "random_percentile": {"development": 0.99, "validation": 0.46}},
                {"rank": 2, "rule": "trend: days 200", "family": "trend", "finalist": False, "development": measures}]}}
    assert "4,238 rules tried" in views.leaderboard_summary(report_json)
    assert views.families_table(report_json).rows[0][0][0] == "turn of month"
    rows = views.leaderboard_table(report_json).rows
    assert rows[0][0][0] == "1 *" and rows[0][7][0] == "-3.9%" and rows[0][9][0] == "46%"
    assert rows[1][6][0] == views.NOTHING   # not a finalist: no validation shown
    assert views.leaderboard_summary(sample_report()) == ""
