#include <gtest/gtest.h>
#include <cmath>
#include "../../src/risk/RiskEngineFactory.hpp"
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

TEST(RiskEngineTests, UnknownEngineIsAnError)
{
    EXPECT_THROW(engine({{"name", "YOLO"}}), std::runtime_error);
}
