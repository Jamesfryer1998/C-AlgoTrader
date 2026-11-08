#include <gtest/gtest.h>
#include "../../src/oms/OrderValidator.hpp"

/**
 * Why the OMS's risk checks block an order: each check in turn, with the reason it gives (saved in
 * trades.csv's note column). The last close is $100, with a 2% slippage tolerance and at most 10 shares.
 */
namespace
{
    const float CLOSE = 100.0f;

    struct OrderValidatorReasonTests : public ::testing::Test
    {
        OrderValidator validator;
        MarketData bars;
        std::vector<Position> positions;

        void SetUp() override
        {
            validator.setParams({{"max_position_size", 10}, {"max_exposure", 5.0}, {"slippage_tolerance", 2.0}});
            std::vector<MarketCondition> history = {MarketCondition("2026-10-26 10:00:00", "KO", CLOSE, CLOSE, 1000, "1m")};
            bars.update(history);
        }

        static Order protectedOrder(OrderType type, float quantity, float price)
        {
            Order order(type, "KO", quantity, price);
            order.setStopLoss(5);
            order.setTakeProfit(10);
            return order;
        }

        // Validates and returns the reason ("" if it passed)
        std::string reason(const Order& order)
        {
            bool passed = validator.validateOrder(order, bars, positions);
            EXPECT_EQ(passed, validator.lastFailure.empty());
            return validator.lastFailure;
        }
    };
}

TEST_F(OrderValidatorReasonTests, AGoodOrderPasses)
{
    EXPECT_EQ(reason(protectedOrder(OrderType::BUY, 5, CLOSE)), "");
    EXPECT_EQ(reason(protectedOrder(OrderType::SELL, 5, CLOSE)), "");
}

TEST_F(OrderValidatorReasonTests, UnknownOrderType)
{
    EXPECT_EQ(reason(protectedOrder(OrderType::HOLD, 5, CLOSE)), "order type HOLD not allowed");
}

TEST_F(OrderValidatorReasonTests, BuyingTooFarAboveTheLastClose)
{
    EXPECT_EQ(reason(protectedOrder(OrderType::BUY, 5, 103)), "price 103 too far from last close 100");
}

TEST_F(OrderValidatorReasonTests, QuantityOutOfRange)
{
    EXPECT_EQ(reason(protectedOrder(OrderType::BUY, 0, CLOSE)), "quantity 0 not between 0 and max_position_size 10");
    EXPECT_EQ(reason(protectedOrder(OrderType::BUY, 11, CLOSE)), "quantity 11 not between 0 and max_position_size 10");
}

TEST_F(OrderValidatorReasonTests, PositionWouldGoBeyondTheLimit)
{
    positions.push_back(Position("KO", 8, CLOSE));
    EXPECT_EQ(reason(protectedOrder(OrderType::BUY, 5, CLOSE)),
              "holding 8, buying 5 would take the position beyond max_position_size 10");
}

TEST_F(OrderValidatorReasonTests, MissingOrWrongStopLoss)
{
    Order noStop(OrderType::BUY, "KO", 5, CLOSE);
    noStop.setTakeProfit(10);
    EXPECT_EQ(reason(noStop), "stop loss 0 missing or on the wrong side of 100");

    Order stopAbove = protectedOrder(OrderType::BUY, 5, CLOSE);
    stopAbove.setStopLoss(-5);   // 105: above the price of a buy
    EXPECT_EQ(reason(stopAbove), "stop loss 105 missing or on the wrong side of 100");
}

TEST_F(OrderValidatorReasonTests, MissingTakeProfit)
{
    Order noTarget(OrderType::SELL, "KO", 5, CLOSE);
    noTarget.setStopLoss(5);
    EXPECT_EQ(reason(noTarget), "take profit 0 missing or on the wrong side of 100");
}

TEST_F(OrderValidatorReasonTests, SellingTooFarAboveTheLastClose)
{
    // A sell above the close passes the price check, but not the slippage one
    EXPECT_EQ(reason(protectedOrder(OrderType::SELL, 5, 102.5f)),
              "slippage between price 102.5 and last close 100 above slippage_tolerance");
}

TEST_F(OrderValidatorReasonTests, RiskEnginesSizedInDollarsHaveNoShareLimit)
{
    // Only FIXED_SIZE keeps max_position_size; the others are capped by their own % of equity
    validator.setParams({{"max_position_size", 10}, {"max_exposure", 5.0}, {"slippage_tolerance", 2.0},
                         {"risk", {{"name", "FIXED_FRACTION"}}}});
    EXPECT_EQ(reason(protectedOrder(OrderType::BUY, 500, CLOSE)), "");

    validator.setParams({{"max_position_size", 10}, {"max_exposure", 5.0}, {"slippage_tolerance", 2.0},
                         {"risk", {{"name", "KELLY"}, {"max_position_size", 50}}}});
    EXPECT_EQ(reason(protectedOrder(OrderType::BUY, 51, CLOSE)), "quantity 51 not between 0 and max_position_size 50");
}

TEST_F(OrderValidatorReasonTests, MissingLimitsAreAConfigError)
{
    OrderValidator unconfigured;
    EXPECT_THROW(unconfigured.setParams({{"max_position_size", 10}}), std::runtime_error);
}

// ---------- Order types ----------

TEST(OrderTypeTests, EveryTradeableTypeReadsBackFromItsName)
{
    // As trades.csv saves and reloads them
    for (OrderType type : {OrderType::BUY, OrderType::SELL, OrderType::LIMIT_BUY, OrderType::LIMIT_SELL,
                           OrderType::STOP_BUY, OrderType::STOP_SELL})
    {
        Order order(type, "KO", 1, CLOSE);
        EXPECT_EQ(stringToOrderType(order.getTypeAsString()), type) << order.getTypeAsString();
    }
    EXPECT_EQ(orderTypeToString(OrderType::HOLD), "HOLD");   // never traded, so never read back
    EXPECT_THROW(stringToOrderType("SIDEWAYS"), std::runtime_error);
}
