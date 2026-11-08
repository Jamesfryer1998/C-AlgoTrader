#pragma once

#include <ostream>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * StartupSummary
 *
 * What the live app is about to do, in one block, printed once everything is connected and back-filled:
 *
 *   ======================================== AlgoTrader ========================================
 *   Mode      LIVE on Alpaca's paper account
 *   Account   ACTIVE, cash $98,150.70, buying power $396,385.69
 *   Market    closed, opens in 1d 4h 59m (Mon 09:30 New York, Mon 14:30 here)
 *   Bars      Alpaca's IEX stream, 1-minute bars
 *   Risk      KELLY: kelly_fraction 0.5, lookback_trades 50, min_trades 20, starting_percent 5
 *   Records   data/live: 43 orders and 5 positions restored
 *
 *   Stock  Strategy  Stop  Target  History  Settings
 *   TXN    MEANREV     1%      2%    60/60  entry_threshold 3, exit_threshold 0, lookback_period 60
 *   ...
 *   ============================================================================================
 *
 * Stop and Target are each strategy's stop_loss and take_profit; History is how many recent bars were
 * back-filled of the number the strategies need.
 */
class StartupSummary
{
    public:
        // One line of the header, e.g. add("Mode", "LIVE on Alpaca's paper account"). Empty text is skipped.
        void add(const std::string& label, const std::string& text);

        // One stock: its active strategies (settings as in config/strategy.json) and its back-fill
        void addStock(const std::string& ticker, const json& strategies, size_t backfilled, int barsNeeded);

        void print(std::ostream& out) const;

        // e.g. "KELLY: kelly_fraction 0.5, lookback_trades 50" (the "risk" block of config/risk.json)
        static std::string describeRisk(const json& risk);

        // A strategy's settings other than its name, whether it's active, and its stop / target,
        // e.g. "entry_threshold 3, lookback_period 60"
        static std::string describeSettings(const json& strategy);

    private:
        struct StockRow
        {
            std::string ticker;
            std::string strategy;
            std::string stop;
            std::string target;
            std::string history;
            std::string settings;
        };

        std::vector<std::pair<std::string, std::string>> header;
        std::vector<StockRow> rows;
};
