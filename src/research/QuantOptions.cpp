#include "QuantOptions.hpp"

#include <stdexcept>
#include "../util/CommandLine.hpp"

namespace
{
    const std::vector<std::string> DEFAULT_RISK_SYMBOLS = {"AAPL", "MSFT", "NVDA", "JPM", "XOM"};
    const std::vector<std::string> DEFAULT_PAIR = {"KO", "PEP"};
    const std::vector<std::string> DEFAULT_SCAN_SYMBOLS = {"KO", "PEP", "XOM", "CVX", "V", "MA",
                                                           "HD", "LOW", "UPS", "FDX", "GS", "MS"};
    const std::string DEFAULT_CROSSOVER_SYMBOL = "SPY";

    QuantOptions::Project projectNamed(const std::string& name)
    {
        if (name == "risk")    return QuantOptions::Project::Risk;
        if (name == "pairs")   return QuantOptions::Project::Pairs;
        if (name == "scan")    return QuantOptions::Project::Scan;
        if (name == "macross") return QuantOptions::Project::Crossover;
        throw std::invalid_argument("Unknown project: " + name);
    }

    std::vector<std::string> defaultSymbols(QuantOptions::Project project)
    {
        switch (project)
        {
            case QuantOptions::Project::Risk:      return DEFAULT_RISK_SYMBOLS;
            case QuantOptions::Project::Pairs:     return DEFAULT_PAIR;
            case QuantOptions::Project::Scan:      return DEFAULT_SCAN_SYMBOLS;
            case QuantOptions::Project::Crossover: return {DEFAULT_CROSSOVER_SYMBOL};
        }
        return {};
    }
}

QuantOptions
QuantOptions::parse(int argc, char* argv[])
{
    QuantOptions options;
    std::string first = argc > 1 ? argv[1] : "";
    if (first.empty() || first == "--help" || first == "help")
    {
        options.showHelp = true;
        return options;
    }
    options.project = projectNamed(first);

    CommandLine args(argc, argv, 2);
    while (args.next())
    {
        if (args.is("--help"))
            options.showHelp = true;
        else if (args.is("--start"))
            options.start = args.text();
        else if (args.is("--end"))
            options.end = args.text();
        else if (args.is("--offline"))
            options.offline = true;
        else if (args.is("--symbols") || args.is("--pair"))
            options.symbols = args.list();
        else if (args.is("--symbol"))
            options.symbols = {args.text()};
        // risk
        else if (args.is("--weights"))
            options.risk.weights = args.numbers();
        else if (args.is("--horizon"))
            options.risk.horizonDays = args.integer();
        else if (args.is("--sims"))
            options.risk.simulations = args.integer();
        else if (args.is("--value"))
            options.risk.portfolioValue = args.number();
        else if (args.is("--seed"))
            options.risk.seed = static_cast<unsigned>(args.integer());
        // pairs
        else if (args.is("--lookback"))
            options.pairs.lookbackDays = args.integer();
        else if (args.is("--entry"))
            options.pairs.entryZ = args.number();
        else if (args.is("--exit"))
            options.pairs.exitZ = args.number();
        else if (args.is("--stop"))
            options.pairs.stopZ = args.number();
        // macross
        else if (args.is("--fast"))
            options.crossover.fastDays = args.integer();
        else if (args.is("--slow"))
            options.crossover.slowDays = args.integer();
        // pairs and macross
        else if (args.is("--cost-bps"))
            options.pairs.costBps = options.crossover.costBps = args.number();
        else if (args.is("--delay"))
            options.pairs.signalDelayDays = options.crossover.signalDelayDays = args.integer();
        else
            args.rejectOption();
    }

    if (options.symbols.empty())
        options.symbols = defaultSymbols(options.project);
    if (options.project == Project::Pairs && options.symbols.size() != 2)
        throw std::invalid_argument("pairs needs exactly 2 stocks, e.g. --pair KO,PEP");
    if (options.project == Project::Scan && options.symbols.size() < 2)
        throw std::invalid_argument("scan needs at least 2 stocks");
    return options;
}

void
QuantOptions::printUsage(std::ostream& out)
{
    out << R"(Quant learning projects (daily data from Alpaca, keys in .env)

Usage: quant_app <project> [options]

  risk      Monte Carlo portfolio risk
              --symbols AAPL,MSFT,NVDA,JPM,XOM   stocks in the portfolio
              --weights 0.3,0.2,0.2,0.2,0.1      (default: equal)
              --horizon 21                       trading days ahead (21 = about a month)
              --sims 10000                       number of simulated futures
              --value 100000                     portfolio value in $
              --seed 42

  pairs     Pairs trading backtest
              --pair KO,PEP          Y,X: regresses log Y on log X
              --lookback 252         days used to fit the hedge ratio and z-score
              --entry 2 --exit 0.5 --stop 4      z-score levels (--stop 0 = no stop)
              --cost-bps 5           cost per trade (0.01% units) on the gross position

  scan      Test every pair in a list for cointegration
              --symbols KO,PEP,XOM,CVX,V,MA,HD,LOW,UPS,FDX,GS,MS

  macross   Moving average crossover backtest
              --symbol SPY --fast 50 --slow 200 --cost-bps 5

Common options:
  --start 2016-01-01   first day of data (Alpaca's history starts in 2016)
  --end YYYY-MM-DD     last day (default: yesterday)
  --offline            use the CSVs already in data/daily/ instead of downloading
  --delay 1            days between a signal and the returns it earns (pairs/macross; 0 = look-ahead)
)";
}
