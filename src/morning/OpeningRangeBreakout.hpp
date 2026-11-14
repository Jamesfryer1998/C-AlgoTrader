#pragma once

#include <optional>
#include <string>
#include <vector>
#include "MorningSettings.hpp"
#include "MorningTrade.hpp"
#include "StockSelection.hpp"
#include "../backtest/TradingCosts.hpp"
#include "../data_access/IntradayBar.hpp"
#include "../risk/DayEnd.hpp"

/**
 * OpeningRangeBreakout
 *
 * One stock's opening range breakout trade on one day (Zarattini, Barbon, Aziz 2024), on 1-minute bars:
 *
 *   opening range   the first opening_minutes bars (09:30 to 09:35): their high and low
 *   direction       up (closed above its open) -> buy above the range's high; down -> short below its low
 *   entry           the first later bar that trades through the level. Filled at the next bar's open (or the
 *                   level, if that's worse for us) with entry_fill "next_open"; at the level, or the bar's open
 *                   if it opened past it, with "touch"
 *   stop            stop_atr_fraction x the 14-day ATR from the entry. Filled at the stop, or the bar's open if
 *                   it opened past it
 *   exit            the stop; else a profit target at target_r x the risk; else hold_minutes after the entry;
 *                   else the end of the day (config/risk.json's end_of_day: the bar ending 15:55, at its close)
 *   size            shares losing risk_percent of equity at the stop, at most max_leverage x equity / stocks
 *
 * At most one trade per stock a day, none from 15:45 (end_of_day). Every fill pays slippage_percent against
 * us, and sells pay the SEC and FINRA fees. Within the entry bar the order of its high and low is unknown, so
 * with "touch" fills it only counts as stopped there if it closed past the stop.
 */
class OpeningRangeBreakout
{
    public:
        static inline const std::string STOP = "stop";
        static inline const std::string TARGET = "target";
        static inline const std::string TIME = "time";
        static inline const std::string CLOSE = "close";
        static inline const std::string END = "end";   // the bars ran out (a half day, a halt)

        OpeningRangeBreakout(const MorningSettings& settings, const MorningSettings::Variant& variant,
                             const TradingCosts& costs, const DayEnd& dayEnd)
            : settings(settings), variant(variant), costs(costs), dayEnd(dayEnd) {}

        // The day's trade, if the price broke out. bars: the stock's 1-minute bars that day, oldest first.
        std::optional<MorningTrade> trade(const StockSelection::Pick& pick, const std::vector<IntradayBar>& bars,
                                          double equity) const;

    private:
        // Fill an exit at `price` (before slippage) on bar `bar`
        void exitAt(MorningTrade& trade, const IntradayBar& bar, double price, const std::string& reason) const;

        MorningSettings settings;
        MorningSettings::Variant variant;
        TradingCosts costs;
        DayEnd dayEnd;
};
