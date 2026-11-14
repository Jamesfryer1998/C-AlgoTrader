#pragma once

#include <string>

/**
 * IntradayBar
 *
 * One bar with all four prices, for research that needs each minute's high and low (a breakout's entry, a
 * stop being touched). The live trader and its backtests use MarketCondition (open and close only).
 *
 *   time   "YYYY-MM-DD HH:MM:SS", New York exchange time, when the bar starts
 */
struct IntradayBar
{
    std::string time;
    double open = 0;
    double high = 0;
    double low = 0;
    double close = 0;
    double volume = 0;
};
