#pragma once

#include <ostream>
#include <string>
#include <vector>
#include "../util/Defaults.hpp"

/**
 * ScreenOptions
 *
 * screen_app's command line. Anything not given comes from src/config/screen.json (see ScreenSettings).
 */
struct ScreenOptions
{
    static constexpr size_t DEFAULT_TOP_PICKS = 5;
    static constexpr double DEFAULT_PORTFOLIO_VALUE = Defaults::STARTING_CAPITAL;   // $ split across the top picks for the risk check
    static constexpr int NOT_GIVEN = -1;

    bool showHelp = false;
    bool daily = false;                 // daily bars since 2016 instead of 1-minute bars
    std::vector<std::string> symbols;   // empty: screen.json's universe
    std::vector<std::string> strategies;   // empty: every strategy in screen.json
    int months = NOT_GIVEN;             // minute bars: months of history
    int testMonths = NOT_GIVEN;         // minute bars: months at the end used as the test period
    std::string testStart;              // daily bars: first day of the test period
    int maxCombinations = NOT_GIVEN;    // try a random sample of each grid instead of all of it
    int threads;                        // backtests at once (default: every CPU core)
    bool downloadOnly = false;
    bool offline = false;
    bool resume = false;
    bool reportOnly = false;
    size_t topPicks = DEFAULT_TOP_PICKS;
    double portfolioValue = DEFAULT_PORTFOLIO_VALUE;

    ScreenOptions();

    // Throws std::invalid_argument with a readable message for unknown options or bad values
    static ScreenOptions parse(int argc, char* argv[]);
    static void printUsage(std::ostream& out);
};
