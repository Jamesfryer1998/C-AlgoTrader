#pragma once

#include <string>
#include <vector>
#include "data_access/IntradayBar.hpp"
#include "morning/MorningSettings.hpp"

// Small helpers for the morning screen's tests
namespace MorningTestBars
{
    inline std::string twoDigits(int number)
    {
        return (number < 10 ? "0" : "") + std::to_string(number);
    }

    // "2026-03-02 09:35:00" for a minute of the day (minutes since midnight)
    inline std::string at(const std::string& day, int minute)
    {
        return day + " " + twoDigits(minute / 60) + ":" + twoDigits(minute % 60) + ":00";
    }

    inline IntradayBar bar(const std::string& day, int minute, double open, double high, double low, double close,
                           double volume = 1000)
    {
        return {at(day, minute), open, high, low, close, volume};
    }

    // A whole session (09:30 to 15:59) at a steady `price`, so a test only spells the minutes it cares about
    inline std::vector<IntradayBar> flatDay(const std::string& day, double price)
    {
        std::vector<IntradayBar> bars;
        for (int minute = 9 * 60 + 30; minute < 16 * 60; minute++)
            bars.push_back(bar(day, minute, price, price, price, price));
        return bars;
    }

    // Replace the bar at `minute` in a day from flatDay
    inline void setBar(std::vector<IntradayBar>& bars, int minute, double open, double high, double low, double close)
    {
        for (auto& existing : bars)
            if (existing.time.substr(11, 5) == twoDigits(minute / 60) + ":" + twoDigits(minute % 60))
            {
                existing.open = open;
                existing.high = high;
                existing.low = low;
                existing.close = close;
            }
    }

    // Settings with simple numbers: 2 stocks a day, $10,000, no slippage unless a test sets it
    inline MorningSettings simpleSettings()
    {
        MorningSettings settings;
        settings.start = "2026-03-02";
        settings.developmentEnd = "2026-03-31";
        settings.validationEnd = "2026-04-30";
        settings.end = "2026-05-29";
        settings.stocks = 2;
        settings.startingCapital = 10000;
        settings.slippagePercent = 0;
        settings.riskPercent = 1.0;
        settings.maxLeverage = 4.0;
        settings.stopAtrFraction = 0.1;
        return settings;
    }

    inline constexpr int minute(int hours, int minutes) { return hours * 60 + minutes; }
}
