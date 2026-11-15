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
    for period, held in board.get("buy_and_hold", {}).items():
        print(f"  {board['symbol']} held, {period}: cagr {held['cagr']:+.3f} sharpe {held['sharpe']:+.2f} "
              f"dd {held['max_drawdown']:.3f}")
    for family in board["families"]:
        print(f"  {family['family']:20} tried {family['tried']:5} eligible {family['eligible']:5} best dev "
              f"{family['best_development_edge']:+.3f} median dev {family['median_development_edge']:+.3f} "
              f"median val {family['median_validation_edge']:+.3f} val ahead {family['validation_ahead']:.2f}")
    for row in board["top"][:rows]:
        d = row["development"]
        line = (f"  {row['rank']:3}{'*' if row['finalist'] else ' '} {row['rule'][:62]:62} exp {d['exposure']:.2f} "
                f"cagr {d['cagr']:+.3f} edge {d['edge']:+.3f} capture {d['capture']:.2f} sharpe {d['sharpe']:+.2f}")
        if "max_drawdown" in d:
            line += f" dd {d['max_drawdown']:.3f} trades {d['trades']}"
        if "stocks_ahead" in row:
            line += f" stocks ahead {row['stocks_ahead']['development']:.2f}"
        for period, label in (("validation", "VAL"), ("holdout", "HOLDOUT")):
            if period in row:
                v = row[period]
                line += f" | {label} exp {v['exposure']:.2f} cagr {v['cagr']:+.3f} edge {v['edge']:+.3f} capture {v['capture']:.2f}"
                if "random_percentile" in row:
                    line += (f" sharpe {v['sharpe']:+.2f} dd {v['max_drawdown']:.3f} trades {v['trades']} "
                             f"beat random {row['random_percentile'][period]:.2f}")
                if "stocks_ahead" in row:
                    line += f" stocks ahead {row['stocks_ahead'][period]:.2f}"
        print(line)
    picks = board.get("stock_picks")
    if picks:
        print("\nEACH STOCK'S BEST RULE")
        for period, summary in picks["summary"].items():
            print(f"  {period:12} " + "  ".join(f"{key} {value:+.3f}" for key, value in summary.items()))
        for row in picks["rows"][:rows]:
            d, v = row["development"], row["validation"]
            print(f"  {row['symbol']:6} {row['rule'][:58]:58} dev edge {d['edge']:+.3f} exp {d['exposure']:.2f} | VAL cagr "
                  f"{v['cagr']:+.3f} held {v['held_cagr']:+.3f} edge {v['edge']:+.3f} sharpe {v['sharpe']:+.2f} held "
                  f"{v['held_sharpe']:+.2f} beat random {v['random_percentile']:.2f}"
                  + (f" | HOLDOUT cagr {row['holdout']['cagr']:+.3f} held {row['holdout']['held_cagr']:+.3f} edge "
                     f"{row['holdout']['edge']:+.3f} beat random {row['holdout']['random_percentile']:.2f}" if "holdout" in row else ""))


if __name__ == "__main__":
    main(sys.argv[1])
