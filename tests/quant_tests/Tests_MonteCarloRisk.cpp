#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <random>
#include "Synthetic.hpp"
#include "quant/MonteCarloRisk.hpp"

using namespace quant;

namespace
{
    // Two stocks with a chosen correlation of daily returns
    PriceTable correlatedPair(double rho, size_t days = 1500, double vol = 0.015)
    {
        std::mt19937_64 random(5);
        std::normal_distribution<double> normal(0.0, 1.0);
        PriceTable table;
        table.symbols = {"AAA", "BBB"};
        table.dates = synthetic::weekdays(days);
        table.closes = {{100.0}, {50.0}};
        for (size_t t = 1; t < days; t++)
        {
            double z1 = normal(random), z2 = normal(random);
            table.closes[0].push_back(table.closes[0].back() * std::exp(vol * z1));
            table.closes[1].push_back(table.closes[1].back() * std::exp(vol * (rho * z1 + std::sqrt(1 - rho * rho) * z2)));
        }
        return table;
    }
}

TEST(MonteCarloRiskTests, VarMatchesTheNormalFormulaForOneStock)
{
    // One stock, no drift, 1% daily vol, 21 days: the 5% worst outcome is exp(-1.645 * 0.01 * sqrt(21)) - 1
    Vector simulated = MonteCarloRisk::simulate({0.0}, {{0.0001}}, {1.0}, 21, 50000, 1);
    TailRisk risk = MonteCarloRisk::tailRisk(simulated, 0.95);
    double expected = 1.0 - std::exp(-1.6449 * 0.01 * std::sqrt(21.0));
    EXPECT_NEAR(risk.var, expected, 0.003);
    EXPECT_GT(risk.es, risk.var);   // the average of the tail is worse than its edge
    // For a normal distribution ES95 is about 1.25 x VaR95
    EXPECT_NEAR(risk.es / risk.var, 2.0627 / 1.6449, 0.06);
}

TEST(MonteCarloRiskTests, TailRiskOfAKnownList)
{
    Vector returns;
    for (int i = 1; i <= 100; i++)
        returns.push_back(-0.10 + 0.002 * i);   // -9.8% .. +10%
    TailRisk risk = MonteCarloRisk::tailRisk(returns, 0.95);
    EXPECT_NEAR(risk.var, 0.0882, 1e-3);   // 5th percentile
    EXPECT_NEAR(risk.es, 0.094, 1e-3);     // average of the worst 5
}

TEST(MonteCarloRiskTests, CorrelationDrivesPortfolioRisk)
{
    RiskSettings settings;
    settings.simulations = 20000;

    RiskResult together = MonteCarloRisk::run(correlatedPair(0.9), settings);
    EXPECT_NEAR(together.correlation[0][1], 0.9, 0.03);
    EXPECT_NEAR(together.weights[0], 0.5, 1e-12);   // equal weights by default

    // Same stocks with correlation removed: less risk
    EXPECT_GT(together.at95.var, together.uncorrelated95.var * 1.2);
    EXPECT_GT(together.at95.es, together.uncorrelated95.es * 1.2);
    EXPECT_GT(together.portfolioAnnualVolatility, together.portfolioAnnualVolatilityUncorrelated);

    // Barely correlated stocks: the two numbers are close
    RiskResult apart = MonteCarloRisk::run(correlatedPair(0.0), settings);
    EXPECT_NEAR(apart.at95.var, apart.uncorrelated95.var, 0.01);
    EXPECT_LT(apart.at95.var, together.at95.var);
    EXPECT_GT(apart.diversificationRatio, together.diversificationRatio);
}

TEST(MonteCarloRiskTests, ResultsAreSortedAndHistoryIsChecked)
{
    RiskSettings settings;
    settings.simulations = 2000;
    settings.horizonDays = 21;
    settings.weights = {3, 1};   // normalised to 75% / 25%
    PriceTable prices = correlatedPair(0.5, 600);

    RiskResult r = MonteCarloRisk::run(prices, settings);
    EXPECT_NEAR(r.weights[0], 0.75, 1e-12);
    ASSERT_EQ(r.simulatedReturns.size(), 2000u);
    EXPECT_TRUE(std::is_sorted(r.simulatedReturns.begin(), r.simulatedReturns.end()));
    EXPECT_GT(r.at99.var, r.at95.var);
    EXPECT_EQ(r.historicalWindows, 600 - 21);
    EXPECT_GE(r.historicalBreachRate95, r.historicalBreachRate99);
    EXPECT_FALSE(r.worstHistoricalStart.empty());

    // Same seed, same answer
    EXPECT_DOUBLE_EQ(MonteCarloRisk::run(prices, settings).at95.var, r.at95.var);
}

TEST(MonteCarloRiskTests, RejectsBadInput)
{
    RiskSettings settings;
    settings.weights = {1, 2, 3};   // three weights, two stocks
    EXPECT_THROW(MonteCarloRisk::run(correlatedPair(0.5, 300), settings), std::runtime_error);

    PriceTable twins = correlatedPair(0.5, 300);
    twins.closes[1] = twins.closes[0];   // identical stocks: covariance can't be factorised
    EXPECT_THROW(MonteCarloRisk::run(twins, RiskSettings()), std::runtime_error);

    EXPECT_THROW(MonteCarloRisk::run(correlatedPair(0.5, 30), RiskSettings()), std::runtime_error);
}
