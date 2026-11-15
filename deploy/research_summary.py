"""Print a study's report.json (data/<study>/report.json) as plain text, for the Server status workflow."""

import json
import sys

PERIODS = ["development", "validation", "holdout"]


def main(path: str) -> None:
    try:
        with open(path) as file:
            report = json.load(file)
    except (OSError, ValueError) as error:
        print(f"No report yet ({error})")
        return
    print(f"generated {report['generated_utc']}  dates {report['dates']}  symbols {report['universe_symbols']}  "
          f"days traded {report['days_traded']}  downloads {report['downloads']}  holdout {report['holdout_opened']}")
    print("\nSTEP 1", json.dumps(report["step1"], indent=1))
    print("\nRESULTS")
    for row in sorted(report["results"], key=lambda r: (PERIODS.index(r["period"]), r["variant"], r["selection"])):
        print(f"  {row['period']:12} {row['variant'][:26]:26} {row['selection']:10} ret {row['total_return']:+.3f} "
              f"sharpe {row['sharpe']:+.2f} dd {row['max_drawdown']:.3f} trades {row['trades']:6} "
              f"win {row['win_rate']:.2f} net/trade {row['avg_net_per_trade']:+.2f} cost/trade "
              f"{row['avg_cost_per_trade']:.2f} gross/cost {row['gross_to_cost']:+.2f} dsr {row['deflated_sharpe']}")
    print("\nGATES")
    for gate in report["gates"]:
        print(f"  {'PASS' if gate['pass'] else 'FAIL'} {gate['step']} {gate['variant']} ({gate['period']})")
        for check in gate["checks"]:
            print(f"       {'ok' if check['pass'] else 'no'} {check['name']}: {check['detail']}")

    board = report.get("leaderboard")
    if board:
        print_leaderboard(board)


def print_leaderboard(board: dict, rows: int = 40) -> None:
    """timing_app's search: the summary, each family's medians and the best rules on development."""
    print(f"\nLEADERBOARD  {board['rules_tried']} rules, {board['eligible']} eligible, ahead of the mix "
          f"{board['ahead_of_mix']}, rank correlation {board['edge_rank_correlation']:+.3f}")
    for period, held in board["buy_and_hold"].items():
        print(f"  {board['symbol']} held, {period}: cagr {held['cagr']:+.3f} sharpe {held['sharpe']:+.2f} "
              f"dd {held['max_drawdown']:.3f}")
    for family in board["families"]:
        print(f"  {family['family']:20} tried {family['tried']:5} eligible {family['eligible']:5} best dev "
              f"{family['best_development_edge']:+.3f} median dev {family['median_development_edge']:+.3f} "
              f"median val {family['median_validation_edge']:+.3f} val ahead {family['validation_ahead']:.2f}")
    for row in board["top"][:rows]:
        d = row["development"]
        line = (f"  {row['rank']:3}{'*' if row['finalist'] else ' '} {row['rule'][:62]:62} exp {d['exposure']:.2f} "
                f"cagr {d['cagr']:+.3f} edge {d['edge']:+.3f} capture {d['capture']:.2f} sharpe {d['sharpe']:+.2f} "
                f"dd {d['max_drawdown']:.3f} trades {d['trades']}")
        if "validation" in row:
            v = row["validation"]
            line += (f" | VAL exp {v['exposure']:.2f} cagr {v['cagr']:+.3f} edge {v['edge']:+.3f} capture "
                     f"{v['capture']:.2f} sharpe {v['sharpe']:+.2f} dd {v['max_drawdown']:.3f} trades {v['trades']} "
                     f"beat random {row['random_percentile']['validation']:.2f}")
        print(line)


if __name__ == "__main__":
    main(sys.argv[1])
