#include "LiveOptions.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include "../util/CommandLine.hpp"

namespace
{
    // --positions KO:10@58.20,PEP:-5@170.10
    const char POSITION_SEPARATOR = ',';
    const char QUANTITY_SEPARATOR = ':';
    const char PRICE_SEPARATOR = '@';
    const std::string POSITIONS_EXAMPLE = "--positions KO:10@58.20,PEP:-5@170.10 (quantity, - for short, @ average price)";

    CarriedPosition parsePosition(const std::string& text)
    {
        size_t colon = text.find(QUANTITY_SEPARATOR), at = text.find(PRICE_SEPARATOR);
        if (colon == std::string::npos || at == std::string::npos || colon == 0 || at < colon)
            throw std::invalid_argument("Can't read the position \"" + text + "\": use " + POSITIONS_EXAMPLE);
        CarriedPosition position;
        position.ticker = text.substr(0, colon);
        try
        {
            position.quantity = std::stod(text.substr(colon + 1, at - colon - 1));
            position.averagePrice = std::stod(text.substr(at + 1));
        }
        catch (const std::exception&)
        {
            throw std::invalid_argument("Can't read the position \"" + text + "\": use " + POSITIONS_EXAMPLE);
        }
        if (position.quantity == 0 || position.averagePrice <= 0)
            throw std::invalid_argument("The position \"" + text + "\" needs a quantity and a price above 0");
        return position;
    }

    std::vector<CarriedPosition> parsePositions(const std::string& list)
    {
        std::vector<CarriedPosition> positions;
        std::istringstream items(list);
        std::string item;
        while (std::getline(items, item, POSITION_SEPARATOR))
            if (!item.empty())
                positions.push_back(parsePosition(item));
        return positions;
    }
}

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
        else if (args.is("--positions"))
            options.backtestPositions = parsePositions(args.text());
        else if (args.is("--portfolio"))
            options.backtestPortfolio = args.text();
        else if (args.is("--records"))
            options.backtestRecords = args.text();
        else
            args.rejectOption();
    }
    if (options.backtestCapital > 0 && !options.backtest)
        throw std::invalid_argument("--capital only works with --backtest: live trading uses the Alpaca account's "
                                    "own money");
    if (!options.backtestPositions.empty() && !options.backtest)
        throw std::invalid_argument("--positions only works with --backtest: live trading holds what the Alpaca "
                                    "account holds");
    if ((!options.backtestPortfolio.empty() || !options.backtestRecords.empty()) && !options.backtest)
        throw std::invalid_argument("--portfolio and --records only work with --backtest: live trading uses "
                                    "config/strategy.json and its own records");
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
  --positions KO:10@58.20,PEP:-5@170.10
                            start the backtest holding these (quantity, - for short, @ average price), e.g.
                            what the live account carried into that day. Their value at the last close
                            before the backtest comes out of the starting capital's cash
  --portfolio picks.json    backtest the stocks and strategies in this file (a "portfolio" list, like
                            config/strategy.json's or screen_app's top_picks_config.json) instead
  --records picks/2026-11-10
                            keep the backtest's records here, under data/ (default: backtest/<first day>)
  --quiet                   no minute tables, just the recap at the end
  --verbose                 also show each start-up step (config files, connections, back-fill)
)";
}
