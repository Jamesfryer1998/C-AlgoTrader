#include "LiveOptions.hpp"

#include <algorithm>
#include <stdexcept>

namespace
{
    // The value after an option, as a number; a readable error if it isn't one
    double numberAfter(const std::string& option, const std::string& value)
    {
        try
        {
            size_t used = 0;
            double number = std::stod(value, &used);
            if (used == value.size())
                return number;
        }
        catch (const std::exception&)
        {
        }
        throw std::invalid_argument("Bad value for " + option + ": " + value);
    }
}

LiveOptions
LiveOptions::parse(int argc, char* argv[])
{
    LiveOptions options;
    for (int i = 1; i < argc; i++)
    {
        std::string option = argv[i];
        bool valueFollows = i + 1 < argc && argv[i + 1][0] != '-';
        auto nextValue = [&]() -> std::string {
            if (!valueFollows)
                throw std::invalid_argument("Missing value for " + option);
            return argv[++i];
        };

        if (option == "--help")
            options.showHelp = true;
        else if (option == "--replay")
        {
            options.replay = true;
            if (valueFollows)
                options.replayDay = argv[++i];
        }
        else if (option == "--days")
            options.replayDays = std::max(1, static_cast<int>(numberAfter(option, nextValue())));
        else if (option == "--speed")
            options.replaySpeed = std::max(0.0, numberAfter(option, nextValue()));
        else
            throw std::invalid_argument("Unknown option: " + option);
    }
    return options;
}

void
LiveOptions::printUsage(std::ostream& out)
{
    out << R"(Live trading on Alpaca (stocks and strategies from src/config/config.json)

Usage: algo_trader_app [options]
  (no options)              trade live on Alpaca's stream (paper or live account, from config.json)
  --replay [YYYY-MM-DD]     replay saved 1-minute bars with a simulated broker instead: no orders reach
                            Alpaca and no keys are needed (default day: the latest saved for every stock)
  --days 3                  replay this many trading days from that day (default 1)
  --speed 10                replayed minutes per second (default 10; 0 = as fast as possible)
)";
}
