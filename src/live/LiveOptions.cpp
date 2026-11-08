#include "LiveOptions.hpp"

#include <algorithm>
#include <stdexcept>
#include "../util/CommandLine.hpp"

LiveOptions
LiveOptions::parse(int argc, char* argv[])
{
    LiveOptions options;
    CommandLine args(argc, argv);
    while (args.next())
    {
        if (args.is("--help"))
            options.showHelp = true;
        else if (args.is("--backtest"))
        {
            options.backtest = true;
            if (args.hasValue())
                options.backtestDay = args.text();
        }
        else if (args.is("--days"))
            options.backtestDays = std::max(1, args.integer());
        else if (args.is("--quiet"))
            options.quiet = true;
        else if (args.is("--verbose"))
            options.verbose = true;
        else if (args.is("--speed"))
            options.backtestSpeed = std::max(0.0, args.number());
        else if (args.is("--capital"))
        {
            options.backtestCapital = args.number();
            if (options.backtestCapital <= 0)
                throw std::invalid_argument("--capital needs an amount above 0, e.g. --capital 25000");
        }
        else
            args.rejectOption();
    }
    if (options.backtestCapital > 0 && !options.backtest)
        throw std::invalid_argument("--capital only works with --backtest: live trading uses the Alpaca account's "
                                    "own money");
    return options;
}

void
LiveOptions::printUsage(std::ostream& out)
{
    out << R"(Live trading on Alpaca (stocks and strategies from config/strategy.json)

Usage: algo_trader_app [options]
  (no options)              trade live on Alpaca's stream (paper or live account, from config/general.json)
  --backtest [YYYY-MM-DD]   backtest 1-minute bars with a simulated broker instead: no orders reach Alpaca.
                            Bars not saved for that day (and the days before, for warm-up) are downloaded
                            first, which needs the keys in .env. No day: the latest saved for every stock
  --days 3                  backtest this many trading days from that day (default 1)
  --speed 10                simulated minutes per second, to watch it run (default 0: as fast as possible)
  --capital 25000           the backtest's starting capital in $ (default: "starting_capital" in
                            config/general.json)
  --quiet                   no minute tables, just the recap at the end
  --verbose                 also show each start-up step (config files, connections, back-fill)
)";
}
