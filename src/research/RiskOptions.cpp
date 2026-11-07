#include "RiskOptions.hpp"

#include <algorithm>
#include <stdexcept>
#include "../util/CommandLine.hpp"

RiskOptions
RiskOptions::parse(int argc, char* argv[])
{
    RiskOptions options;
    options.replay.replay = true;
    CommandLine args(argc, argv);
    while (args.next())
    {
        if (args.is("--help"))               options.replay.showHelp = true;
        else if (args.is("--replay"))        options.replay.replayDay = args.text();
        else if (args.is("--days"))          options.replay.replayDays = std::max(1, args.integer());
        else if (args.is("--engines"))       options.engines = args.list();
        else if (args.is("--tune"))          options.tune = true;
        else if (args.is("--test-days"))     options.testDays = std::max(1, args.integer());
        else if (args.is("--max-drawdown"))  options.maxDrawdownPercent = args.number();
        else if (args.is("--save"))          options.save = true;
        else                                 args.rejectOption();
    }
    if (!options.replay.showHelp && options.replay.replayDay.empty())
        throw std::invalid_argument("Say which days to replay, e.g. --replay 2025-11-03 --days 120");
    if (options.save && !options.tune)
        throw std::invalid_argument("--save goes with --tune");
    return options;
}

void
RiskOptions::printUsage(std::ostream& out)
{
    out << R"(Risk engines (position sizing) on replayed days, with the portfolio in config.json

Compare the engines:
  risk_app --replay 2025-11-03 --days 120 [--engines RISK_PER_TRADE,FIXED_FRACTION]

Tune their settings: try each engine's grid on the training days, check the best on later test days,
and pick the setting that made the most without falling more than --max-drawdown in either period:
  risk_app --tune --replay 2025-11-03 --days 120 --test-days 40 [--max-drawdown 10] [--save]

  --replay 2025-11-03   first day (bars not saved are downloaded, as for algo_trader_app)
  --days 120            trading days to replay (tuning: the training days)
  --engines A,B         only these engines (default: all)
  --test-days 40        tuning: days after the training days (default: half as many as --days)
  --max-drawdown 10     tuning: the largest fall from a high allowed, % (default: config or 10)
  --save                tuning: write the chosen settings into config.json's "risk"

Engines: FIXED_SIZE, FIXED_FRACTION, RISK_PER_TRADE, VOLATILITY_TARGET. Settings to compare are in
config.json's "risk_comparison", the grids to tune in "risk_tuning" (defaults if missing).
)";
}
