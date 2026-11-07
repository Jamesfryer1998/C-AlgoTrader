#pragma once

#include <ostream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "RiskOptions.hpp"
#include "../live/LiveTrader.hpp"

using json = nlohmann::json;

/**
 * RiskComparison
 *
 * Which risk engine suits the portfolio? Replays the same saved days once per engine, with the portfolio
 * and strategies in config.json and only "risk" changed, then compares them side by side:
 *
 *   Risk engine                Return  Max drawdown  Sharpe  Most capital in use  Fills
 *   -------------------------  ------  ------------  ------  -------------------  -----
 *   FIXED_SIZE 1                +1.67%         0.40%    3.10      $3,557 (3.6%)    3995
 *
 * The engines and their settings come from config.json's "risk_comparison" list; without one, each engine
 * runs with its defaults. Each replay's records go to data/replay/<day>/ as usual (the last one is kept).
 */
class RiskComparison
{
    public:
        RiskComparison(const json& config, const std::string& dataRoot, const RiskOptions& options);

        // Runs every engine; prints progress and the table. 1 if a replay failed.
        int run(std::ostream& out);

        // The "risk" blocks to try: config "risk_comparison", or every engine with its defaults.
        // names (if given) keeps only those engines.
        static std::vector<json> engines(const json& config, const std::vector<std::string>& names);

        // One quiet replay of `days` days from firstDay with this "risk" block (ok = false if it failed)
        static LiveTrader::Result replayWith(const json& config, const std::string& dataRoot, const json& risk,
                                             const std::string& firstDay, int days, bool& ok);

        // e.g. "RISK_PER_TRADE 0.50%"
        static std::string label(const json& config, const json& risk);

        static double returnOf(const LiveTrader::Result& result);

        // A result as a row of the table below (columns: TABLE_COLUMNS)
        static std::vector<std::string> tableRow(const std::string& name, const LiveTrader::Result& result);
        static const std::vector<std::string> TABLE_COLUMNS;
        static const std::vector<bool> TABLE_RIGHT_ALIGNED;

    private:
        void printTable(std::ostream& out) const;

        json config;
        std::string dataRoot;
        RiskOptions options;
        std::vector<std::string> labels;
        std::vector<LiveTrader::Result> results;
};
