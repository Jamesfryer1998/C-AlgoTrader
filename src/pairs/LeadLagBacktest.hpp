#pragma once

#include <map>
#include <vector>
#include "PairsSettings.hpp"
#include "PriceMatrix.hpp"
#include "../backtest/TradingCosts.hpp"
#include "../morning/MorningBacktest.hpp"

/**
 * LeadLagBacktest
 *
 * Do smaller stocks follow the bigger ones they move with? (config/pairs.json's "lead_lag".) Each month every
 * stock gets a leader: the bigger stock whose daily returns moved most with its own over the year before (the
 * control: a bigger stock at random). Each day, the stocks whose leader beat them most are bought at the close
 * and those it trailed most are shorted, held hold_days.
 */
class LeadLagBacktest
{
    public:
        LeadLagBacktest(const PairsSettings& settings, const PriceMatrix& prices, const TradingCosts& costs)
            : settings(settings), prices(prices), costs(costs) {}

        // follower -> leader for a month starting at day `formationEnd`. universe: most dollar volume first,
        // so a leader is always earlier in it
        std::map<size_t, size_t> closestLeaders(const std::vector<size_t>& universe, size_t formationEnd) const;
        std::map<size_t, size_t> randomLeaders(const std::vector<size_t>& universe, size_t formationEnd) const;

        void run(const PairsSettings::Variant& variant, bool matched, size_t first, size_t last,
                 MorningBacktest::Line& line) const;

    private:
        PairsSettings settings;
        const PriceMatrix& prices;
        TradingCosts costs;
};
