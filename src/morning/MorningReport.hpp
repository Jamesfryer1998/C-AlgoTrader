#pragma once

#include <ostream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "MorningBacktest.hpp"
#include "MorningSettings.hpp"

using json = nlohmann::json;

/**
 * MorningReport
 *
 * Turns a morning backtest into the plan's verdicts, for each line in each period (development, validation,
 * and the holdout once opened):
 *
 *   performance   return, Sharpe (annualised), drawdown, trades, win rate
 *   costs         average P&L and cost per trade, and gross P&L / costs ("earns more than it costs?")
 *   deflated      the in-play variants' deflated Sharpe, counting every variant tried
 *   gates         step 1 (moves vs costs); steps 3-4 on validation; step 5 on the holdout
 *
 * Saved as data/morning/report.json (the dashboard's Research tab reads it) and trades.csv, and printed.
 */
class MorningReport
{
    public:
        MorningReport(const MorningSettings& settings, const MorningBacktest::Results& results);

        // Everything, as saved to report.json
        json toJson(const std::string& holdoutOpened, size_t downloads) const;

        // Every breakout trade: variant, selection, symbol, day, side, times, prices, shares, P&L, R, why it ended
        bool writeTrades(const std::string& path) const;

        void print(std::ostream& out) const;

        // One line in one period
        struct Row
        {
            std::string variant, selection, period;
            int days = 0, trades = 0;
            double winRate = 0, totalReturn = 0, cagr = 0, sharpe = 0, maxDrawdown = 0;
            double avgNetPerTrade = 0, avgCostPerTrade = 0, grossToCost = 0;
            double deflatedSharpe = -1;   // in-play variants only (-1: not worked out)
        };

        // A pass mark and whether it was met
        struct Check
        {
            std::string name;
            bool pass = false;
            std::string detail;
        };

        struct Gate
        {
            std::string step, variant, period;
            std::vector<Check> checks;
            bool pass() const;
        };

        const std::vector<Row>& rows() const { return table; }
        const std::vector<Gate>& gates() const { return verdicts; }

        // The in-play stocks' typical 10-minute move / the round-trip cost (step 1)
        double moveToCost() const;

    private:
        Row rowFor(const MorningBacktest::Line& line, const std::string& period) const;
        const Row* find(const std::string& variant, const std::string& selection, const std::string& period) const;
        void addDeflatedSharpes(const std::string& period);
        void addGates();

        MorningSettings settings;
        MorningBacktest::Results results;
        std::vector<Row> table;
        std::vector<Gate> verdicts;
        std::vector<std::string> periods;
};

/**
 * MorningHoldout
 *
 * The holdout is looked at once. The first --holdout run records when, in data/morning/holdout_opened.json;
 * later runs still show it but say it was already opened (so its result no longer counts as unseen).
 */
namespace MorningHoldout
{
    // When the holdout was first opened ("" if never)
    std::string openedAt(const std::string& folder);

    // Record that it's being opened now, unless it was already. Returns when it was first opened.
    std::string open(const std::string& folder, const std::vector<std::string>& variants);
}
