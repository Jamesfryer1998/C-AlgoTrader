#include <gtest/gtest.h>
#include "../../src/oms/OrderValidator.hpp"


class OrderValidatorTests : public ::testing::Test {
public:

    OrderValidator cut;
    Config config;

    void SetUp() override 
    {
        config.loadJson("/Users/james/Projects/C++AlgoTrader/tests/strategy_tests/test_data/config_test.json");
        algoTestConfig = config.loadConfig();
    }

    void TearDown() override 
    {

    }

    void GivenWeHaveABuyOrder()
    {
        order = Order{
                "BUY",
                "AAPL",
                100.0,
                109.0};
    }

    void GivenWeHaveASellOrder()
    {
        order = Order{
                "SELL",
                "AAPL",
                100.0,
                109.0};
    }

    void GivenWeHaveAnInvalidOrder()
    {
        order = Order{
                "SELLING",
                "AAPL",
                100.0,
                120.0};
    }

    void GivenWeHaveMarketData()
    {
        string dataFilePath = "/Users/james/Projects/C++AlgoTrader/tests/data_access_tests/test_data/market_data_test_1.csv";
        marketData.loadData(dataFilePath);
    }

    Order order;
    json algoTestConfig;
    MarketData marketData;

};

TEST_F(OrderValidatorTests, CanInstantiate)
{
    OrderValidator cut;
}

TEST_F(OrderValidatorTests, CanSetParams)
{
    cut.setParams(algoTestConfig);
}

TEST_F(OrderValidatorTests, GivenWeHaveABuyOrderType_WeValidateCorrectly)
{
    GivenWeHaveABuyOrder();
    EXPECT_EQ(cut.isValidOrderType(order), true);
}

TEST_F(OrderValidatorTests, GivenWeHaveASellOrderType_WeValidateCorrectly)
{
    GivenWeHaveASellOrder();
    EXPECT_EQ(cut.isValidOrderType(order), true);
}

TEST_F(OrderValidatorTests, GivenWeHaveAnInvalidOrderType_WeValidateCorrectly)
{
    GivenWeHaveAnInvalidOrder();
    EXPECT_EQ(cut.isValidOrderType(order), false);
}

TEST_F(OrderValidatorTests, GivenWeHaveAValidPrice_WeValidateCorrectly)
{
    GivenWeHaveMarketData();
    GivenWeHaveABuyOrder();

    EXPECT_EQ(marketData.getData().size(), 10);
    EXPECT_EQ(cut.isValidPrice(order, marketData), true);
}

TEST_F(OrderValidatorTests, GivenWeHaveAnInvalidPrice_WeValidateCorrectly)
{
    GivenWeHaveMarketData();
    GivenWeHaveAnInvalidOrder();

    EXPECT_EQ(marketData.getData().size(), 10);
    EXPECT_EQ(cut.isValidPrice(order, marketData), false);
}
