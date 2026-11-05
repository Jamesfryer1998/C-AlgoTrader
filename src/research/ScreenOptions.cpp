#include "ScreenOptions.hpp"

#include <algorithm>
#include <thread>
#include "../util/CommandLine.hpp"

ScreenOptions::ScreenOptions()
    : threads(static_cast<int>(std::max(1u, std::thread::hardware_concurrency())))
{
}

ScreenOptions
ScreenOptions::parse(int argc, char* argv[])
{
    ScreenOptions options;
    CommandLine args(argc, argv);
    while (args.next())
    {
        if (args.is("--help"))                  options.showHelp = true;
        else if (args.is("--daily"))            options.daily = true;
        else if (args.is("--symbols"))          options.symbols = args.list();
        else if (args.is("--strategies"))       options.strategies = args.list();
        else if (args.is("--months"))           options.months = std::max(MIN_HISTORY_MONTHS, args.integer());
        else if (args.is("--test-months"))      options.testMonths = std::max(1, args.integer());
        else if (args.is("--test-start"))       options.testStart = args.text();
        else if (args.is("--max-combinations")) options.maxCombinations = args.integer();
        else if (args.is("--threads"))          options.threads = std::max(1, args.integer());
        else if (args.is("--download-only"))    options.downloadOnly = true;
        else if (args.is("--offline"))          options.offline = true;
        else if (args.is("--resume"))           options.resume = true;
        else if (args.is("--report"))           options.reportOnly = true;
        else if (args.is("--top"))              options.topPicks = static_cast<size_t>(std::max(1, args.integer()));
        else if (args.is("--value"))            options.portfolioValue = args.number();
        else                                    args.rejectOption();
    }
    return options;
}

void
ScreenOptions::printUsage(std::ostream& out)
{
    out << R"(Strategy screen: tune RSI / MACD / MEANREV / SCALP per stock on a training period, judge on a later test period

Usage: screen_app [options]
  --daily                   daily bars since 2016 instead of 1-minute bars (settings then mean days)
  --symbols AAPL,MSFT,...   stocks to screen (default: "universe" in src/config/screen.json, the S&P 100)
  --strategies RSI,MEANREV  strategies to try (default: all in screen.json)
  --months 24               minute bars: months of history (default: from screen.json)
  --test-months 6           minute bars: months at the end used as the test period (default: from screen.json)
  --test-start 2024-01-01   daily bars: first day of the test period (default: from screen.json)
  --max-combinations 300    try a fixed random sample of each grid instead of all of it (faster)
  --threads 8               backtests at once (default: all CPU cores)
  --download-only           just download the bars (e.g. overnight), don't screen
  --offline                 use the bars already saved instead of downloading
  --resume                  keep stocks already in the results file and screen the rest
  --report                  just print the report from the saved results
  --top 5                   how many combinations to take forward and risk-check together (default 5)
  --value 100000            money split across the top picks, for the risk numbers in $ (default 100000)
)";
}
