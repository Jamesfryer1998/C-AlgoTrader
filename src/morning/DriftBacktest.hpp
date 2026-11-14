#pragma once

#include <map>
#include <string>
#include <vector>
#include "MorningBacktest.hpp"
#include "MorningSettings.hpp"
#include "StockSelection.hpp"
#include "../backtest/TradingCosts.hpp"
#include "../quant/DailyBars.hpp"

/**
 * DriftBacktest
 *
 * Do stocks in play keep going for days? Each day's picks are bought (or shorted) at that day's close, in the
 * direction of their opening bar (or of the whole day, with "direction": "day"; against it with "fade"), held
 * hold_days trading days and sold at the close. Only daily bars are needed.
 *
 *   size    each day's picks share drift.leverage x equity / hold_days, so overlapping days never hold more
 *           than leverage x equity in all: stocks x hold_days positions of equal value
 *   costs   slippage_percent on both fills, the SEC and FINRA fees on sells (a short's entry, a long's exit).
 *           A short's borrowing cost isn't counted
 *   P&L     marked to each day's close, so the line's daily returns include positions still held
 */
class DriftBacktest
{
    public:
        // symbol -> date -> its daily bar
        using DailyBySymbol = std::map<std::string, std::map<std::string, quant::DailyBar>>;
        // day -> the stocks picked that morning
        using PicksByDay = std::map<std::string, std::vector<StockSelection::Pick>>;

        DriftBacktest(const MorningSettings& settings, const TradingCosts& costs) : settings(settings), costs(costs) {}

        // Trade `variant` on the picks, day by day through `calendar` (trading days, oldest first)
        void run(const MorningSettings::Variant& variant, const PicksByDay& picks, const DailyBySymbol& daily,
                 const std::vector<std::string>& calendar, MorningBacktest::Line& line) const;

    private:
        MorningSettings settings;
        TradingCosts costs;
};
