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
 *   algo_trader_app --replay [YYYY-MM-DD] [--days N] [--speed M]
 *                                                    replay saved bars with a simulated broker
 */
struct LiveOptions
{
    static constexpr double DEFAULT_REPLAY_SPEED = 10;   // replayed minutes per real second

    bool showHelp = false;
    bool quiet = false;   // only the recap: no start-up notes or minute tables
    bool replay = false;
    std::string replayDay;   // empty: the latest day saved for every stock
    int replayDays = 1;
    double replaySpeed = DEFAULT_REPLAY_SPEED;   // 0 = as fast as possible

    // Throws std::invalid_argument with a readable message for unknown or malformed options
    static LiveOptions parse(int argc, char* argv[]);
    static void printUsage(std::ostream& out);
};
