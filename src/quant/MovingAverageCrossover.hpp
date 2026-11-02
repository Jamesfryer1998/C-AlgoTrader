#pragma once

#include <string>
#include <vector>
#include "Stats.hpp"

/**
 * Project 3: Moving average crossover backtest
 *
 * The simplest strategy there is: be invested while the 50-day average price is above the 200-day
 * average ("golden cross"), and in cash while it's below. The point is the testing:
 *
 *   - No look-ahead: the signal from day t's close only earns returns from day t+1 (signalDelayDays = 1).
 *     For comparison the backtest is also run with no delay, which quietly uses each day's closing price
 *     to decide that same day's position. It isn't achievable, so any edge it shows is fake (the faster
 *     the strategy trades, the bigger that fake edge gets).
 *   - Costs: every buy or sell pays costBps (0.01% units), shown with and without.
 *   - Benchmark: buy and hold over exactly the same days. Most strategies fail here.
 */
namespace quant
{
    struct CrossoverSettings
    {
        int fastDays = 50;
        int slowDays = 200;
        double costBps = 5.0;       // per buy or sell, e.g. 5 = 0.05% (commission + spread + slippage)
        int signalDelayDays = 1;
    };

    struct CrossoverResult
    {
        std::vector<std::string> dates;   // days the backtest covers (from the first day both averages exist)
        Vector closes;
        Vector fastAverage;
        Vector slowAverage;
        Vector position;                  // 1 invested, 0 in cash, held during each day
        Vector strategyReturns;           // after costs
        Vector buyHoldReturns;

        Performance strategy;             // delayed signal, with costs  <- the honest result
        Performance strategyNoCosts;
        Performance lookAhead;            // no delay, with costs        <- the "too good to be true" result
        Performance buyHold;              // with one buy's cost

        int trades = 0;                   // round trips started (buys)
        double timeInMarket = 0;
        double costsPaid = 0;             // total cost as a fraction of capital (sum of daily cost drags)
    };

    class MovingAverageCrossover
    {
        public:
            // dates and closes are daily, oldest first
            static CrossoverResult backtest(const std::vector<std::string>& dates, const Vector& closes,
                                            const CrossoverSettings& settings);

            // Average of the last `days` closes at each day; NaN until there are enough
            static Vector simpleMovingAverage(const Vector& closes, int days);
    };
}
