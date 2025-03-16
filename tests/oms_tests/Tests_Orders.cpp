#include <gtest/gtest.h>
#include "../../src/oms/Order.hpp"

TEST(OrderTests, CanBeInstantiated)
{
    Order cut{
            "Filled",
            "AAPL",
            100.0,
            120.0};
}

TEST(OrderTests, CanSetAndGetValues)
{
    Order cut{
            "Filled",
            "AAPL",
            100.0,
            120.0};

    cut.setId(1);
    cut.setPrice(50.0);
    cut.setType("BUY");
    cut.setQuantity(300);
    cut.setTicker("GOOG");

    EXPECT_EQ(cut.getId(), 1);
    EXPECT_EQ(cut.getPrice(), 50.0);
    EXPECT_EQ(cut.getQuantity(), 300);
    EXPECT_EQ(cut.getTicker(), "GOOG");
    EXPECT_EQ(cut.getType(), "BUY");
}

TEST(OrderTests, StopLossPriceForLongIsCorrectlyCalculated)
{
    Order cut{
            "BUY",
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
    Order cut{
            "SELL",
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
    Order cut{
            "BUY",
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
    Order cut{
            "SELL",
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