#pragma once

#include <map>
#include <ostream>
#include <string>
#include <vector>
#include "../backtest/Screener.hpp"
#include "../quant/MonteCarloRisk.hpp"

/**
 * TopPicksReport
 *
 * The combinations to take forward (Screener::topPicks), saved as top_picks.csv and as config.json strategy
 * blocks ready to paste (top_picks_config.json), and the Monte Carlo risk of trading them together:
 * each pick's daily P&L with its chosen setting, simulated forward a month at a time.
 */
class TopPicksReport
{
    public:
        // bars: each pick's saved bars by symbol (for the risk check)
        TopPicksReport(const std::vector<Screener::Row>& rows, const Screener& screener, size_t count,
                       double portfolioValue, const std::map<std::string, std::vector<MarketCondition>>& bars);

        const std::vector<Screener::Row>& getPicks() const { return picks; }

        void print(std::ostream& out) const;

        // Saves top_picks.csv and top_picks_config.json in `folder`; returns their paths
        std::vector<std::string> save(const std::string& folder) const;

    private:
        void printPicks(std::ostream& out) const;
        void printRisk(std::ostream& out) const;

        const Screener& screener;
        size_t count;
        double portfolioValue;
        std::vector<Screener::Row> picks;
        quant::PriceTable pickValues;   // each pick's value at the end of every day
};
