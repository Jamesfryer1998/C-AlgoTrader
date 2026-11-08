#pragma once

#include <ostream>
#include <string>
#include <vector>

/**
 * LiveOptions
 *
 * The live app's command line:
 *
 *   algo_trader_app                                  trade live on Alpaca
 *   algo_trader_app --backtest [YYYY-MM-DD] [--days N] [--speed M] [--capital $]
 *                                                    backtest saved bars with a simulated broker
 */
struct LiveOptions
{
    static constexpr double DEFAULT_BACKTEST_SPEED = 0;   // simulated minutes per second; 0 = as fast as possible

    bool showHelp = false;
    bool quiet = false;    // --quiet: no start-up notes or minute tables, just the recap at the end
    bool verbose = false;  // --verbose: also each start-up step (files loaded, connections, back-fill)
    bool silent = false;   // nothing at all (risk_app runs many backtests and prints its own table)
    bool backtest = false;
    std::string backtestDay;   // empty: the latest day saved for every stock
    int backtestDays = 1;
    double backtestSpeed = DEFAULT_BACKTEST_SPEED;   // 0 = as fast as possible
    double backtestCapital = 0;   // --capital: the backtest's starting capital; 0 = "starting_capital" in config/general.json

    // Throws std::invalid_argument with a readable message for unknown or malformed options
    static LiveOptions parse(int argc, char* argv[]);
    static void printUsage(std::ostream& out);
};
