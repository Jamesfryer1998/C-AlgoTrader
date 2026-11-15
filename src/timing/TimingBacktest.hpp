#pragma once

#include <random>
#include <string>
#include <vector>
#include "TimingSeries.hpp"
#include "TimingSettings.hpp"
#include "../morning/MorningBacktest.hpp"

/**
 * TimingBacktest
 *
 * A rule's targets traded from day `first` to `last`. A target set at a close is traded at the next open, so
 * the night before is held at the old exposure and the day at the new one. Money not in the symbol is in the
 * cash fund. Each change pays slippage on what's sold and what's bought (both legs when there's a cash fund);
 * a fractional target is only followed when it's rebalance_band away (all in and all out always are).
 *
 * The comparisons:
 *   the exposure-matched mix   the symbol at the rule's average exposure every day, the rest in cash: what
 *                              holding less, without timing, would have made
 *   random timing              the rule's own stretches in and out, in a random order: the same exposure and
 *                              as many trades, at random times
 */
class TimingBacktest
{
    public:
        struct Traded
        {
            std::vector<std::string> days;
            std::vector<double> returns;     // after costs
            std::vector<double> exposure;    // held through each day
            std::vector<int> changes;        // 1 on a day the exposure changed
            std::vector<double> costs;       // as a fraction of the account
        };

        struct Measures
        {
            int days = 0;
            double cagr = 0, sharpe = 0, maxDrawdown = 0, totalReturn = 0;
            double exposure = 0;             // average
            int trades = 0;                  // changes of exposure
            double mixCagr = 0;              // the exposure-matched mix's
            double edge = 0;                 // cagr - mixCagr: what the timing added
            double capture = 0;              // cagr / the symbol's own
        };

        TimingBacktest(const TimingSettings& settings, const TimingSeries& series);

        Traded trade(const std::vector<double>& targets, size_t first, size_t last) const;

        // The same targets' stretches in a random order (over days first - 1 to last - 1, as traded)
        std::vector<double> shuffled(const std::vector<double>& targets, size_t first, size_t last,
                                     std::mt19937_64& random) const;

        // Over the days of `traded` that fall in `period`
        Measures measure(const Traded& traded, const std::string& period) const;

        // The symbol bought and held, as a traded line (exposure 1 throughout, no costs)
        Traded buyAndHold(size_t first, size_t last) const;

        // As a report line: one StockDay a day (its changes as trades)
        MorningBacktest::Line line(const Traded& traded, const std::string& variant, const std::string& selection) const;

        static double spearman(const std::vector<double>& a, const std::vector<double>& b);

    private:
        const TimingSettings& settings;
        const TimingSeries& series;
        MorningSettings periods;
};
