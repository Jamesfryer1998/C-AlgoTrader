#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "MorningSettings.hpp"
#include "MorningTrade.hpp"
#include "StockSelection.hpp"
#include "../backtest/TradingCosts.hpp"
#include "../data_access/IntradayBar.hpp"

using json = nlohmann::json;

/**
 * ScalpDay
 *
 * A live strategy (SCALP) on one picked stock for one day, through the real backtester (StrategyTester), so
 * it trades exactly as it would live: the same code, flat by the close (config/risk.json's end_of_day), the
 * same slippage. It gets one stock's share of the account: max_leverage x equity / stocks.
 *
 * Run twice, with and without costs, so what the costs took is known exactly.
 */
class ScalpDay
{
    public:
        // config: the merged config (end_of_day, regulatory fees); strategy: its settings, e.g. {"name": "SCALP", ...}
        ScalpDay(const json& config, const json& strategy, const MorningSettings& settings, const TradingCosts& costs);

        StockDay run(const StockSelection::Pick& pick, const std::vector<IntradayBar>& bars, double equity) const;

    private:
        json testerConfig(const std::string& symbol) const;

        json config;
        json strategy;
        MorningSettings settings;
        TradingCosts costs;
};
