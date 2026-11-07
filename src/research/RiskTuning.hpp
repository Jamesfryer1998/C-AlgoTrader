#pragma once

#include <ostream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "RiskOptions.hpp"
#include "../live/LiveTrader.hpp"

using json = nlohmann::json;

/**
 * RiskTuning
 *
 * Finds the risk engine settings to trade with, the way the screen tunes strategies:
 *
 *   1. Training: backtest the training days once for every setting in each engine's grid
 *   2. Shortlist: the settings that made the most without falling more than max_drawdown_percent
 *   3. Test: backtest the shortlist on the days after training, which no setting was chosen on
 *   4. Pick: the shortlisted setting with the best weaker return of the two periods, if it also stayed within
 *      the drawdown limit on the test days; with --save it goes into config.json's "risk"
 *
 * Why a drawdown limit and not the best Sharpe: sizing mostly scales a strategy up or down, which barely
 * changes its Sharpe ratio. The real choice is how big a fall you'll accept for the extra return.
 *
 * The grids are config.json's "risk_tuning", e.g.
 *   "risk_tuning": {"max_drawdown_percent": 10, "finalists": 3,
 *                   "grids": {"RISK_PER_TRADE": {"risk_percent": [0.25, 0.5, 1], "max_stock_percent": [20, 40]}}}
 * Engines without a grid there use the defaults in RiskTuning.cpp. FIXED_SIZE is run as the baseline.
 */
class RiskTuning
{
    public:
        RiskTuning(const json& config, const std::string& dataRoot, const std::string& configPath,
                   const RiskOptions& options);

        // 0 when a setting was chosen (and saved with --save), 1 if none passed or a backtest failed
        int run(std::ostream& out);

        // Every combination in each engine's grid, as "risk" blocks
        static std::vector<json> grid(const json& config, const std::vector<std::string>& engines);

        // Replace config.json's "risk" line (and the note above it) without touching the rest of the file
        static bool saveToConfig(const std::string& configPath, const json& risk, const std::string& note);

        // {"name": "X", "a": 1} on one line, name first
        static std::string oneLine(const json& risk);

    private:
        struct Candidate
        {
            json risk;
            std::string name;
            LiveTrader::Result train;
            LiveTrader::Result test;
        };

        bool withinLimit(const LiveTrader::Result& result) const;
        bool backtest(Candidate& candidate, bool testPeriod, std::ostream& out);
        void printTable(std::ostream& out, const std::vector<Candidate>& rows, bool withTest) const;

        json config;
        std::string dataRoot;
        std::string configPath;
        RiskOptions options;
        double maxDrawdown;   // fraction
        size_t finalists;
        int testDays;
        std::string testFirstDay;
};
