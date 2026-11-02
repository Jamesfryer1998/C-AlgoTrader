#pragma once

#include <string>
#include <utility>
#include <vector>
#include "DailyBars.hpp"
#include "Stats.hpp"

/**
 * Project 2: Pairs trading (statistical arbitrage)
 *
 * Two stocks that move together (e.g. KO and PEP) are "cointegrated" if a fixed mix of them, the spread,
 * keeps returning to its average:
 *
 *   spread = log(Y) - hedgeRatio * log(X) - alpha       (hedgeRatio and alpha from regressing log Y on log X)
 *   z      = spread / standard deviation of the spread  (how stretched it is right now)
 *
 * Trading rules, checked at each day's close:
 *   z above +entry  -> Y is expensive: short the spread (sell Y, buy hedgeRatio of X)
 *   z below -entry  -> Y is cheap: long the spread (buy Y, sell hedgeRatio of X)
 *                      (no new trade while |z| is beyond the stop level)
 *   |z| back under exit  -> take profit, the spread reverted
 *   |z| beyond stop      -> the relationship may have broken: cut the loss
 *
 * No look-ahead: the hedge ratio and the spread's volatility are fitted only on the `lookbackDays` up to the
 * signal day, and a signal from day t's close only earns returns from day t+1 (signalDelayDays = 1).
 * The hedge ratio is frozen for the life of each trade. Each trade has $1 of gross exposure, split
 * between the legs ($1/(1+h) of Y against $h/(1+h) of X), and pays costBps each time it is opened or closed.
 *
 * Relationships break: the cointegration test is also run year by year to show how stable the pair was.
 */
namespace quant
{
    struct PairsSettings
    {
        int lookbackDays = 252;     // days used to fit the hedge ratio and the spread's volatility
        double entryZ = 2.0;
        double exitZ = 0.5;
        double stopZ = 4.0;         // 0 = no stop
        double costBps = 5.0;       // cost per unit of gross exposure traded (both legs together), in 0.01%
        int signalDelayDays = 1;    // trade at the close this many days after the signal
    };

    struct PairTrade
    {
        std::string entryDate;      // close the trade was opened at
        std::string exitDate;       // close it was closed at ("open" if still running)
        int side = 0;               // +1 long spread (buy Y, sell X), -1 short spread
        double hedgeRatio = 0;
        double entryZ = 0;
        double exitZ = 0;
        double returnPct = 0;       // on gross exposure, after costs
        int days = 0;
        std::string exitReason;     // "reverted", "stop", "end of data"
    };

    struct PairsResult
    {
        std::string y, x;
        CointegrationResult fullSample;
        std::vector<std::pair<std::string, CointegrationResult>> byYear;   // "2019" -> test on that year
        double returnCorrelation = 0;

        std::vector<std::string> dates;   // trading days the backtest covers
        Vector z;                         // z-score of the spread each day (rolling fit, or the trade's frozen fit)
        Vector hedgeRatio;                // rolling hedge ratio
        Vector position;                  // held during each day: +1, -1 or 0
        Vector dailyReturns;              // strategy, after costs
        std::vector<PairTrade> trades;
        Performance performance;
        double timeInMarket = 0;
    };

    // One row of a pair scan: how strongly two stocks are cointegrated
    struct PairScan
    {
        std::string y, x;
        CointegrationResult test;
        double returnCorrelation = 0;
    };

    class PairsTrading
    {
        public:
            // y and x are closing prices on the same dates
            static PairsResult backtest(const std::string& yName, const std::string& xName,
                                        const std::vector<std::string>& dates, const Vector& y, const Vector& x,
                                        const PairsSettings& settings);

            // Test every pair of stocks in the table; most cointegrated first
            static std::vector<PairScan> scan(const PriceTable& prices);

            // Engle-Granger test for each calendar year with enough data
            static std::vector<std::pair<std::string, CointegrationResult>>
            testByYear(const std::vector<std::string>& dates, const Vector& y, const Vector& x);
    };
}
