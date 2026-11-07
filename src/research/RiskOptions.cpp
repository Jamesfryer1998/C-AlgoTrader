#include "RiskOptions.hpp"

#include <algorithm>
#include <stdexcept>
#include "../util/CommandLine.hpp"

RiskOptions
RiskOptions::parse(int argc, char* argv[])
{
    RiskOptions options;
    options.period.backtest = true;
    CommandLine args(argc, argv);
    while (args.next())
    {
        if (args.is("--help"))               options.period.showHelp = true;
        else if (args.is("--backtest"))      options.period.backtestDay = args.text();
        else if (args.is("--days"))          options.period.backtestDays = std::max(1, args.integer());
        else if (args.is("--engines"))       options.engines = args.list();
        else if (args.is("--tune"))          options.tune = true;
        else if (args.is("--test-days"))     options.testDays = std::max(1, args.integer());
        else if (args.is("--max-drawdown"))  options.maxDrawdownPercent = args.number();
        else if (args.is("--save"))          options.save = true;
        else                                 args.rejectOption();
    }
    if (!options.period.showHelp && options.period.backtestDay.empty())
        throw std::invalid_argument("Say which days to backtest, e.g. --backtest 2025-11-03 --days 120");
    if (options.save && !options.tune)
        throw std::invalid_argument("--save goes with --tune");
    return options;
}

void
RiskOptions::printUsage(std::ostream& out)
{
    out << R"(Risk engines (position sizing) on backtested days, with the portfolio in config/strategy.json

Compare the engines:
  risk_app --backtest 2025-11-03 --days 120 [--engines RISK_PER_TRADE,FIXED_FRACTION]

Tune their settings: try each engine's grid on the training days, check the best on later test days,
and pick the setting that made the most without falling more than --max-drawdown in either period:
  risk_app --tune --backtest 2025-11-03 --days 120 --test-days 40 [--max-drawdown 10] [--save]

  --backtest 2025-11-03   first day (bars not saved are downloaded, as for algo_trader_app)
  --days 120            trading days to backtest (tuning: the training days)
  --engines A,B         only these engines (default: all)
  --test-days 40        tuning: days after the training days (default: half as many as --days)
  --max-drawdown 10     tuning: the largest fall from a high allowed, % (default: config or 10)
  --save                tuning: write the chosen settings into config/risk.json's "risk"

Engines: FIXED_SIZE, FIXED_FRACTION, RISK_PER_TRADE, VOLATILITY_TARGET. Settings to compare are in
config/risk.json's "risk_comparison", the grids to tune in "risk_tuning" (defaults if missing).
)";
}
