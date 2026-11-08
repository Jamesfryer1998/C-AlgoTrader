#pragma once

#include <map>
#include <string>
#include <vector>
#include "Stats.hpp"

/**
 * Daily price bars for the quant projects.
 *
 *   data/daily/KO.csv
 *   Date,Open,High,Low,Close,Volume
 *   2026-10-29,70.12,70.55,69.80,70.31,12345678
 *
 * Prices are adjusted for splits and dividends (Alpaca "adjustment=all"), so returns include
 * dividends and a split doesn't look like a crash.
 */
namespace quant
{
    struct DailyBar
    {
        std::string date;   // YYYY-MM-DD (New York trading day)
        double open = 0;
        double high = 0;
        double low = 0;
        double close = 0;
        double volume = 0;
    };

    bool writeDailyCsv(const std::string& path, const std::vector<DailyBar>& bars);
    std::vector<DailyBar> readDailyCsv(const std::string& path);   // empty if the file is missing

    // Closing prices of several stocks on the days they all traded, oldest first
    struct PriceTable
    {
        std::vector<std::string> symbols;
        std::vector<std::string> dates;
        Matrix closes;   // closes[stock][day]

        size_t days() const { return dates.size(); }
        const Vector& operator[](size_t stock) const { return closes[stock]; }
    };

    // Keep only the dates every stock has, so day i means the same day for all of them.
    // Optionally clip to [startDate, endDate] (YYYY-MM-DD, empty = no limit).
    PriceTable alignCloses(const std::vector<std::string>& symbols,
                           const std::map<std::string, std::vector<DailyBar>>& bars,
                           const std::string& startDate = "", const std::string& endDate = "");
}
