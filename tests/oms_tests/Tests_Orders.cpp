#include <gtest/gtest.h>
#include "../../src/oms/Order.hpp"

TEST(OrderTests, CanBeInstantiated)
{
    // Test with enum
    oms::Order cut1{
            OrderType::BUY,
            "AAPL",
            100.0,
            120.0};
            
    // Test with string for backwards compatibility
    oms::Order cut2{
            "Filled",
            "AAPL",
            100.0,
            120.0};
}

TEST(OrderTests, OrderTypeConversion)
{
    // Test string to enum conversion
    EXPECT_EQ(stringToOrderType("BUY"), OrderType::BUY);
    EXPECT_EQ(stringToOrderType("buy"), OrderType::BUY);
    EXPECT_EQ(stringToOrderType("SELL"), OrderType::SELL);
    EXPECT_EQ(stringToOrderType("sell"), OrderType::SELL);
    EXPECT_EQ(stringToOrderType("LIMIT_BUY"), OrderType::LIMIT_BUY);
    EXPECT_EQ(stringToOrderType("limit_buy"), OrderType::LIMIT_BUY);
    EXPECT_EQ(stringToOrderType("LIMIT_SELL"), OrderType::LIMIT_SELL);
    EXPECT_EQ(stringToOrderType("limit_sell"), OrderType::LIMIT_SELL);
    
    // Test enum to string conversion
    EXPECT_EQ(orderTypeToString(OrderType::BUY), "BUY");
    EXPECT_EQ(orderTypeToString(OrderType::SELL), "SELL");
    EXPECT_EQ(orderTypeToString(OrderType::LIMIT_BUY), "LIMIT_BUY");
    EXPECT_EQ(orderTypeToString(OrderType::LIMIT_SELL), "LIMIT_SELL");
}

TEST(OrderTests, CanSetAndGetValues)
{
    oms::Order cut{
            OrderType::BUY,
            "AAPL",
            100.0,
            120.0};

    cut.setId(1);
    cut.setPrice(50.0);
    cut.setType(OrderType::SELL);
    cut.setQuantity(300);
    cut.setTicker("GOOG");

    EXPECT_EQ(cut.getId(), 1);
    EXPECT_EQ(cut.getPrice(), 50.0);
    EXPECT_EQ(cut.getQuantity(), 300);
    EXPECT_EQ(cut.getTicker(), "GOOG");
    EXPECT_EQ(cut.getType(), OrderType::SELL);
    EXPECT_EQ(cut.getTypeAsString(), "SELL");
    
    // Test setting type with string
    cut.setType("LIMIT_BUY");
    EXPECT_EQ(cut.getType(), OrderType::LIMIT_BUY);
    EXPECT_EQ(cut.getTypeAsString(), "LIMIT_BUY");
}

TEST(OrderTests, HelperMethods)
{
    oms::Order buyOrder(OrderType::BUY, "AAPL", 100, 100);
    oms::Order sellOrder(OrderType::SELL, "AAPL", 100, 100);
    oms::Order limitBuyOrder(OrderType::LIMIT_BUY, "AAPL", 100, 100);
    oms::Order limitSellOrder(OrderType::LIMIT_SELL, "AAPL", 100, 100);
    
    // Test isBuy() and isSell()
    EXPECT_TRUE(buyOrder.isBuy());
    EXPECT_FALSE(buyOrder.isSell());
    
    EXPECT_TRUE(sellOrder.isSell());
    EXPECT_FALSE(sellOrder.isBuy());
    
    EXPECT_TRUE(limitBuyOrder.isBuy());
    EXPECT_FALSE(limitBuyOrder.isSell());
    
    EXPECT_TRUE(limitSellOrder.isSell());
    EXPECT_FALSE(limitSellOrder.isBuy());
    
    // Test isLimit() and isMarket()
    EXPECT_FALSE(buyOrder.isLimit());
    EXPECT_TRUE(buyOrder.isMarket());
    
    EXPECT_FALSE(sellOrder.isLimit());
    EXPECT_TRUE(sellOrder.isMarket());
    
    EXPECT_TRUE(limitBuyOrder.isLimit());
    EXPECT_FALSE(limitBuyOrder.isMarket());
    
    EXPECT_TRUE(limitSellOrder.isLimit());
    EXPECT_FALSE(limitSellOrder.isMarket());
}

TEST(OrderTests, StopLossPriceForLongIsCorrectlyCalculated)
{
    oms::Order cut{
            OrderType::BUY,
            "AAPL",
            100.0,
            100.0};

    {
        cut.setStopLoss(10);
        EXPECT_EQ(cut.getStopLossPrice(), 90);   
    }
    {
        cut.setStopLoss(100);
        EXPECT_EQ(cut.getStopLossPrice(), 0);   
    }
    {
        cut.setStopLoss(50);
        EXPECT_EQ(cut.getStopLossPrice(), 50);   
    }
    {
        cut.setStopLoss(33.3);
        EXPECT_NEAR(cut.getStopLossPrice(), 66.7, 0.01);
    }
    {
        cut.setStopLoss(99);
        EXPECT_EQ(cut.getStopLossPrice(), 1);   
    }
    {
        cut.setStopLoss(-10);
        EXPECT_EQ(cut.getStopLossPrice(), 110);   
    }
}

TEST(OrderTests, StopLossPriceForShortIsCorrectlyCalculated)
{
    oms::Order cut{
            OrderType::SELL,
            "AAPL",
            100.0,
            100.0};

    {
        cut.setStopLoss(10);
        EXPECT_EQ(cut.getStopLossPrice(), 110);   
    }
    {
        cut.setStopLoss(100);
        EXPECT_EQ(cut.getStopLossPrice(), 200);   
    }
    {
        cut.setStopLoss(50);
        EXPECT_EQ(cut.getStopLossPrice(), 150);   
    }
    {
        cut.setStopLoss(33.3);
        EXPECT_NEAR(cut.getStopLossPrice(), 133.30, 0.01);
    }
    {
        cut.setStopLoss(99);
        EXPECT_EQ(cut.getStopLossPrice(), 199);   
    }
    {
        cut.setStopLoss(-10);
        EXPECT_EQ(cut.getStopLossPrice(), 90);   
    }
}

TEST(OrderTests, TakeProfitPriceForLongIsCorrectlyCalculated)
{
    oms::Order cut{
            OrderType::BUY,
            "AAPL",
            100.0,
            100.0};

    {
        cut.setTakeProfit(10);
        EXPECT_EQ(cut.getTakeProfitPrice(), 110);   
    }
    {
        cut.setTakeProfit(100);
        EXPECT_EQ(cut.getTakeProfitPrice(), 200);   
    }
    {
        cut.setTakeProfit(50);
        EXPECT_EQ(cut.getTakeProfitPrice(), 150);   
    }
    {
        cut.setTakeProfit(33.3);
        EXPECT_NEAR(cut.getTakeProfitPrice(), 133.30, 0.01);
    }
    {
        cut.setTakeProfit(99);
        EXPECT_EQ(cut.getTakeProfitPrice(), 199);   
    }
    {
        cut.setTakeProfit(-10);
        EXPECT_EQ(cut.getTakeProfitPrice(), 90);   
    }
}

TEST(OrderTests, TakeProfitPriceForShortIsCorrectlyCalculated)
{
    oms::Order cut{
            OrderType::SELL,
            "AAPL",
            100.0,
            100.0};

    {
        cut.setTakeProfit(10);
        EXPECT_EQ(cut.getTakeProfitPrice(), 90);   
    }
    {
        cut.setTakeProfit(100);
        EXPECT_EQ(cut.getTakeProfitPrice(), 0);   
    }
    {
        cut.setTakeProfit(50);
        EXPECT_EQ(cut.getTakeProfitPrice(), 50);   
    }
    {
        cut.setTakeProfit(33.3);
        EXPECT_NEAR(cut.getTakeProfitPrice(), 66.7, 0.01);
    }
    {
        cut.setTakeProfit(99);
        EXPECT_EQ(cut.getTakeProfitPrice(), 1);   
    }
    {
        cut.setTakeProfit(-10);
        EXPECT_EQ(cut.getTakeProfitPrice(), 110);   
    }
}