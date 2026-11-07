#include "LiveOptions.hpp"

#include <algorithm>
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
        else if (args.is("--replay"))
        {
            options.replay = true;
            if (args.hasValue())
                options.replayDay = args.text();
        }
        else if (args.is("--days"))
            options.replayDays = std::max(1, args.integer());
        else if (args.is("--speed"))
            options.replaySpeed = std::max(0.0, args.number());
        else
            args.rejectOption();
    }
    return options;
}

void
LiveOptions::printUsage(std::ostream& out)
{
    out << R"(Live trading on Alpaca (stocks and strategies from src/config/config.json)

Usage: algo_trader_app [options]
  (no options)              trade live on Alpaca's stream (paper or live account, from config.json)
  --replay [YYYY-MM-DD]     replay 1-minute bars with a simulated broker instead: no orders reach Alpaca.
                            Bars not saved for that day (and the days before, for warm-up) are downloaded
                            first, which needs the keys in .env. No day: the latest saved for every stock
  --days 3                  replay this many trading days from that day (default 1)
  --speed 10                replayed minutes per second (default 10; 0 = as fast as possible)
)";
}
