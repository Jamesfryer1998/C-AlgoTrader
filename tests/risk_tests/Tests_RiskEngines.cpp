#include <gtest/gtest.h>
#include <cmath>
#include "../../src/risk/RiskEngineFactory.hpp"
#include "../../src/risk/KellyRisk.hpp"
#include "../../src/risk/VolatilityTargetRisk.hpp"

namespace
{
    std::unique_ptr<RiskEngine> engine(json risk)
    {
        return RiskEngineFactory::create({{"risk", risk}});
    }

    // A $100 buy with a 1% stop loss ($99)
    Order buyAt100()
    {
        Order order(OrderType::BUY, "AAA", 1, 100);
        order.setStopLoss(1);
        return order;
    }

    RiskEngine::SizingContext account(double equity, float held = 0, double otherPositions = 0)
    {
        RiskEngine::SizingContext context;
        context.equity = equity;
        context.heldQuantity = held;
        context.otherPositionsValue = otherPositions;
        return context;
    }
}

TEST(RiskEngineTests, NoRiskBlockMeansOneShareOrders)
{
    auto risk = RiskEngineFactory::create(json::object());
    EXPECT_EQ(risk->describe(), "FIXED_SIZE 1");
    auto sized = risk->size(buyAt100(), account(100000, 5));   // adds one more, as before
    ASSERT_TRUE(sized);
    EXPECT_FLOAT_EQ(sized->getQuantity(), 1);
}

TEST(RiskEngineTests, FixedFractionBuysAShareOfTheAccount)
{
    auto risk = engine({{"name", "FIXED_FRACTION"}, {"position_percent", 10}});
    auto sized = risk->size(buyAt100(), account(100000));
    ASSERT_TRUE(sized);
    EXPECT_FLOAT_EQ(sized->getQuantity(), 100);   // $10,000 / $100
}

TEST(RiskEngineTests, RiskPerTradeLosesTheSameIfStopped)
{
    auto risk = engine({{"name", "RISK_PER_TRADE"}, {"risk_percent", 0.5}, {"max_stock_percent", 100}});
    auto sized = risk->size(buyAt100(), account(100000));
    ASSERT_TRUE(sized);
    EXPECT_NEAR(sized->getQuantity(), 500, 1);   // $500 at risk / $1 a share to the stop
}

TEST(RiskEngineTests, RiskPerTradeNeedsAStopLoss)
{
    auto risk = engine({{"name", "RISK_PER_TRADE"}});
    Order noStop(OrderType::BUY, "AAA", 1, 100);
    EXPECT_FALSE(risk->size(noStop, account(100000)));
}

TEST(RiskEngineTests, CapsLimitTheStockAndThePortfolio)
{
    auto risk = engine({{"name", "FIXED_FRACTION"}, {"position_percent", 50}, {"max_stock_percent", 20},
                        {"max_portfolio_percent", 100}});
    EXPECT_FLOAT_EQ(risk->size(buyAt100(), account(100000))->getQuantity(), 200);          // 20% cap: $20,000
    EXPECT_FLOAT_EQ(risk->size(buyAt100(), account(100000, 0, 95000))->getQuantity(), 50); // $5,000 room left
    EXPECT_FALSE(risk->size(buyAt100(), account(100000, 0, 99950)));                       // under a share of room
    EXPECT_FALSE(risk->size(buyAt100(), account(100000, 0, 100000)));                      // no room
}

TEST(RiskEngineTests, OpensOnceAndClosesAgainstThePosition)
{
    auto risk = engine({{"name", "FIXED_FRACTION"}, {"position_percent", 10}});
    EXPECT_FALSE(risk->size(buyAt100(), account(100000, 60)));   // already long: a repeated BUY adds nothing

    Order sell(OrderType::SELL, "AAA", 1, 100);
    auto close = risk->size(sell, account(100000, 100));
    ASSERT_TRUE(close);
    EXPECT_FLOAT_EQ(close->getQuantity(), 100);   // the whole position
}

TEST(RiskEngineTests, VolatilityTargetSizesCalmStocksBigger)
{
    // Prices alternating +/-0.1% a minute: volatility 0.1% a bar
    MarketData bars;
    for (int i = 0; i <= 60; i++)
        bars.addBar(MarketCondition("2026-10-01 10:00:00", "AAA", 100, i % 2 ? 100.1f : 100.0f, 1000, "1m"));
    double perBar = VolatilityTargetRisk::barVolatility(bars, 60);
    EXPECT_NEAR(perBar, 0.001, 0.0001);

    auto risk = engine({{"name", "VOLATILITY_TARGET"}, {"daily_volatility_percent", 0.25}, {"max_stock_percent", 100}});
    auto context = account(100000);
    context.bars = &bars;
    auto sized = risk->size(buyAt100(), context);
    ASSERT_TRUE(sized);
    double expected = std::floor(250 / (100 * perBar * std::sqrt(390.0)));   // $250 a day / $ move a share a day
    EXPECT_NEAR(sized->getQuantity(), expected, 1);
}

TEST(RiskEngineTests, KellyPositionFromWinsAndLosses)
{
    // 60% win +1%, 40% lose -1%: 0.6 / 0.01 - 0.4 / 0.01 = 20 (2000% of equity)
    std::vector<double> edge = {0.01, 0.01, 0.01, -0.01, -0.01};
    EXPECT_NEAR(KellyRisk::kellyPosition(edge), 20, 1e-9);

    // A coin flip that pays the same either way has no edge
    EXPECT_DOUBLE_EQ(KellyRisk::kellyPosition({0.01, -0.01}), 0);
    // Losing more than it wins: still 0, never negative
    EXPECT_DOUBLE_EQ(KellyRisk::kellyPosition({0.01, -0.02, -0.02}), 0);
    // Nothing to go on, or never a win
    EXPECT_DOUBLE_EQ(KellyRisk::kellyPosition({}), 0);
    EXPECT_DOUBLE_EQ(KellyRisk::kellyPosition({-0.01}), 0);
    // Never a loss: no limit of its own
    EXPECT_TRUE(std::isinf(KellyRisk::kellyPosition({0.01, 0.02})));
}

TEST(RiskEngineTests, KellyStartsAtAFixedShareUntilThereAreEnoughTrades)
{
    auto risk = engine({{"name", "KELLY"}, {"min_trades", 4}, {"starting_percent", 5}});
    std::vector<double> trades = {0.01, -0.01, 0.01};   // 3 of the 4 needed

    auto context = account(100000);
    context.closedTradeReturns = &trades;
    auto sized = risk->size(buyAt100(), context);
    ASSERT_TRUE(sized);
    EXPECT_FLOAT_EQ(sized->getQuantity(), 50);   // 5% of $100,000 at $100
}

TEST(RiskEngineTests, KellyScalesTheEdgeAndIsCapped)
{
    // A small edge on 1% moves: 0.55 / 0.01 - 0.45 / 0.01 = 10 (1000% of equity), half Kelly 500%
    std::vector<double> trades;
    for (int i = 0; i < 11; i++) trades.push_back(0.01);
    for (int i = 0; i < 9; i++) trades.push_back(-0.01);
    auto capped = engine({{"name", "KELLY"}, {"min_trades", 20}, {"kelly_fraction", 0.5}, {"max_stock_percent", 20}});
    auto context = account(100000);
    context.closedTradeReturns = &trades;
    auto sized = capped->size(buyAt100(), context);
    ASSERT_TRUE(sized);
    EXPECT_FLOAT_EQ(sized->getQuantity(), 200);   // the 20% cap, not 500%

    // A tiny edge on wide moves fits under the cap: 0.55 / 0.5 - 0.45 / 0.5 = 0.2, x 0.5 = 10% of equity
    std::vector<double> wide;
    for (double r : trades) wide.push_back(r * 50);
    context.closedTradeReturns = &wide;
    sized = capped->size(buyAt100(), context);
    ASSERT_TRUE(sized);
    EXPECT_FLOAT_EQ(sized->getQuantity(), 100);

    // Only the last lookback_trades count: the last 4 all lost, so no trade
    auto recent = engine({{"name", "KELLY"}, {"min_trades", 4}, {"lookback_trades", 4}});
    std::vector<double> turnedBad = {0.05, 0.05, 0.05, -0.01, -0.01, -0.01, -0.01};
    context.closedTradeReturns = &turnedBad;
    EXPECT_FALSE(recent->size(buyAt100(), context));
}

TEST(RiskEngineTests, UnknownEngineIsAnError)
{
    EXPECT_THROW(engine({{"name", "YOLO"}}), std::runtime_error);
}

TEST(RiskEngineTests, EachEngineDescribesItsSettings)
{
    // As shown in the start-up summary and risk_app's tables
    EXPECT_EQ(engine({{"name", "FIXED_SIZE"}, {"quantity", 3}})->describe(), "FIXED_SIZE 3");
    EXPECT_EQ(engine({{"name", "FIXED_FRACTION"}, {"position_percent", 10}})->describe(), "FIXED_FRACTION 10.0%");
    EXPECT_EQ(engine({{"name", "RISK_PER_TRADE"}, {"risk_percent", 0.5}})->describe(), "RISK_PER_TRADE 0.50%");
    EXPECT_NE(engine({{"name", "VOLATILITY_TARGET"}, {"daily_volatility_percent", 1.0}})->describe().find("VOLATILITY_TARGET"),
              std::string::npos);
    EXPECT_EQ(engine({{"name", "KELLY"}, {"kelly_fraction", 0.5}, {"lookback_trades", 50}, {"min_trades", 20},
                      {"starting_percent", 5}})->describe(),
              "KELLY x0.50 (last 50 trades, 5.0% until 20)");
}

TEST(RiskEngineTests, TheFactoryKnowsEveryEngine)
{
    std::vector<std::string> names = RiskEngineFactory::names();
    for (const char* name : {"FIXED_SIZE", "FIXED_FRACTION", "RISK_PER_TRADE", "VOLATILITY_TARGET", "KELLY"})
    {
        EXPECT_NE(std::find(names.begin(), names.end(), name), names.end()) << name;
        EXPECT_NO_THROW(engine({{"name", name}})) << name;
    }
}
