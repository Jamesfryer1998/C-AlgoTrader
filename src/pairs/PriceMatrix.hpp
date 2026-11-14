#pragma once

#include <map>
#include <string>
#include <vector>
#include "../quant/DailyBars.hpp"

/**
 * PriceMatrix
 *
 * Many stocks' daily closes and dollar volumes on one trading calendar, row per stock, column per day, so a
 * month's formation can compare hundreds of stocks quickly. 0 where a stock had no bar that day.
 */
struct PriceMatrix
{
    std::vector<std::string> days;       // the trading calendar, oldest first
    std::vector<std::string> symbols;
    std::vector<std::vector<double>> close;
    std::vector<std::vector<double>> open;
    std::vector<std::vector<double>> dollarVolume;

    // Add a stock from its daily bars (any not on the calendar are ignored)
    void add(const std::string& symbol, const std::vector<quant::DailyBar>& bars);

    size_t stocks() const { return symbols.size(); }

    // The first trading day of each month from day index `first` on
    std::vector<size_t> monthStarts(size_t first) const;

    // The day index of the first day on or after `date` (days.size() if none)
    size_t indexOnOrAfter(const std::string& date) const;

    // Stocks to consider for a month starting at day `formationEnd`, judged on the `days` days before it: a bar
    // every day, priced above minPrice on the last, average dollar volume at least minDollarVolume. The `count`
    // with the most dollar volume, most first.
    std::vector<size_t> liquid(size_t formationEnd, int days, double minPrice, double minDollarVolume,
                               size_t count) const;

    // close[stock][day] / close[stock][day - 1] - 1, or 0 if either is missing
    double dailyReturn(size_t stock, size_t day) const;
};
