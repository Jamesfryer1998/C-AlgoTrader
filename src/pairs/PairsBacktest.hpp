#pragma once

#include <vector>
#include "PairsSettings.hpp"
#include "PriceMatrix.hpp"
#include "../backtest/TradingCosts.hpp"
#include "../morning/MorningBacktest.hpp"

/**
 * PairsBacktest
 *
 * Pairs trading by the distance method (config/pairs.json's "pairs"). Each month a new set of pairs is formed
 * from the year before and traded for trading_days, so about six months' sets overlap; money is shared so
 * that, all open at once, they hold leverage x equity. The control forms its pairs at random instead.
 */
class PairsBacktest
{
    public:
        struct Pair
        {
            size_t a = 0, b = 0;
            double sigma = 0;    // the standard deviation of their gap over the formation year
        };

        PairsBacktest(const PairsSettings& settings, const PriceMatrix& prices, const TradingCosts& costs)
            : settings(settings), prices(prices), costs(costs) {}

        // The `pairs` pairs (no stock twice) whose prices, each divided by its first, tracked closest over the
        // formation_days before `formationEnd`
        std::vector<Pair> closestPairs(const std::vector<size_t>& universe, size_t formationEnd) const;

        // Pairs drawn at random (repeatably, from the month) from the same universe
        std::vector<Pair> randomPairs(const std::vector<size_t>& universe, size_t formationEnd) const;

        // Trade from day index `first` to `last` (inclusive), adding each day to `line`
        void run(const PairsSettings::Variant& variant, bool matched, size_t first, size_t last,
                 MorningBacktest::Line& line) const;

    private:
        // The standard deviation of the gap between a and b over the formation year
        double gapSigma(size_t a, size_t b, size_t formationEnd) const;

        PairsSettings settings;
        const PriceMatrix& prices;
        TradingCosts costs;
};
