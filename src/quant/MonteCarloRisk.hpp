#pragma once

#include <string>
#include <vector>
#include "DailyBars.hpp"
#include "Stats.hpp"

/**
 * Project 1: Monte Carlo portfolio risk
 *
 * 1. From daily prices, estimate each stock's average daily return, its volatility, and how the
 *    stocks move together (the covariance / correlation matrix).
 * 2. Simulate thousands of possible futures over the horizon (default 21 trading days, about a month).
 *    Each future draws correlated random returns for every stock: independent normal draws z are
 *    turned into correlated ones with the Cholesky factor L of the covariance (returns = mean + L z).
 * 3. The distribution of portfolio returns gives:
 *      Value at Risk (VaR 95%)        the loss you exceed in only 5% of futures (a bad month)
 *      Expected Shortfall (ES 95%)    the average loss in those worst 5% (how bad "bad" gets)
 *
 * To show that correlation drives portfolio risk, the same simulation is repeated with every
 * correlation set to zero (same stocks, same volatilities). And to check the model against reality,
 * VaR is compared with the portfolio's real historical monthly returns: a normal model usually
 * underestimates how often big losses happen ("fat tails").
 */
namespace quant
{
    struct RiskSettings
    {
        Vector weights;              // one per stock, normalised to sum to 1 (empty = equal weights)
        int horizonDays = 21;
        int simulations = 10000;
        unsigned seed = 42;
        double portfolioValue = 100000;
    };

    struct TailRisk
    {
        double var = 0;   // loss as a positive fraction of the portfolio, e.g. 0.08 = 8%
        double es = 0;
    };

    struct RiskResult
    {
        std::vector<std::string> symbols;
        Vector weights;
        Vector annualReturn;          // per stock, from the average daily log return
        Vector annualVolatility;      // per stock
        Matrix correlation;
        double portfolioAnnualVolatility = 0;
        double portfolioAnnualVolatilityUncorrelated = 0;   // if every correlation were 0
        double diversificationRatio = 0;    // weighted sum of stock volatilities / portfolio volatility

        Vector simulatedReturns;      // horizon returns of the portfolio, sorted worst first
        double meanReturn = 0;
        double probabilityOfLoss = 0;
        TailRisk at95, at99;
        TailRisk uncorrelated95, uncorrelated99;

        // Real overlapping horizon-length windows from the price history
        int historicalWindows = 0;
        double historicalBreachRate95 = 0;   // fraction of real windows that lost more than VaR 95 (model says 5%)
        double historicalBreachRate99 = 0;   // model says 1%
        double worstHistorical = 0;          // worst real horizon return
        std::string worstHistoricalStart;    // first day of that window
    };

    class MonteCarloRisk
    {
        public:
            static RiskResult run(const PriceTable& prices, RiskSettings settings);

            // Horizon returns of the portfolio for `simulations` random futures (unsorted).
            // dailyMean / dailyCovariance are of daily log returns.
            static Vector simulate(const Vector& dailyMean, const Matrix& dailyCovariance, const Vector& weights,
                                   int horizonDays, int simulations, unsigned seed);

            // VaR and ES at a confidence level (e.g. 0.95) from simulated returns
            static TailRisk tailRisk(Vector returns, double confidence);
    };
}
