"""Print data/morning/report.json (morning_app's report) as plain text, for the Server status workflow."""

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


if __name__ == "__main__":
    main(sys.argv[1])
