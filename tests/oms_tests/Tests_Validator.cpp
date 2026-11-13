#include <gtest/gtest.h>
#include "../../src/oms/OrderValidator.hpp"


class OrderValidatorTests : public ::testing::Test {
public:

    OrderValidator cut;
    Config config;

    void SetUp() override 
    {
        config.loadJson(config.getTestPath("strategy_tests/test_data/config_test.json"));
        algoTestConfig = config.loadConfig();
        cut.setParams(algoTestConfig);
    }

    void TearDown() override 
    {

    }

    void GivenWeHaveABuyOrder()
    {
        order = Order{
                OrderType::BUY,
                "AAPL",
                10,
                109.0};

        // positions.push_back(Position{"AAPL", 10, 109.0});
    }

    void GivenWeHaveASellOrder()
    {
        order = Order{
                OrderType::SELL,
                "AAPL",
                10,
                109.0};

        // positions.push_back(Position{"AAPL", 10, 109.0});
    }

    void GivenWeHaveAnInvalidOrder()
    {
        order = Order{
                OrderType::UNKNOWN, 
                "AAPL",
                20,
                120.00};

        // positions.push_back(Position{"AAPL", 20, 120.0});
    }

    void GivenWeHaveMarketData()
    {
        string dataFilePath = config.getTestPath("data_access_tests/test_data/market_data_test_1.csv");
        marketData.loadData(dataFilePath);
    }

    Order order;
    Position position;
    json algoTestConfig;
    MarketData marketData;
    std::vector<Position> positions;

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

TEST_F(OrderValidatorTests, GivenWeHaveAValidQuantity_WeValidateCorrectly)
{
    GivenWeHaveABuyOrder();
    EXPECT_EQ(cut.isValidQuantity(order), true);
}

TEST_F(OrderValidatorTests, GivenWeHaveAnInvalidQuantity_WeValidateCorrectly)
{
    GivenWeHaveAnInvalidOrder();
    EXPECT_EQ(cut.isValidQuantity(order), false);
}

TEST_F(OrderValidatorTests, GivenPriceAboveStopLoss_WeValidateCorrectly)
{
    GivenWeHaveMarketData();
    GivenWeHaveABuyOrder();
    order.setStopLoss(10);
    
    EXPECT_EQ(cut.checkStopLoss(order, marketData), true);
}

TEST_F(OrderValidatorTests, GivenPriceAboveTakeProfit_WeValidateCorrectly)
{
    GivenWeHaveMarketData();
    GivenWeHaveABuyOrder();
    order.setTakeProfit(10);
    
    EXPECT_EQ(cut.checkTakeProfit(order, marketData), true);
}

TEST_F(OrderValidatorTests, GivenPriceWithinSlippageTollerance_WeValidateCorrectly)
{
    GivenWeHaveMarketData();
    GivenWeHaveABuyOrder();
    
    EXPECT_EQ(cut.checkSlippage(order, marketData), true);
}

TEST_F(OrderValidatorTests, GivenPriceOutOfSlippageTollerance_WeValidateCorrectly)
{
    GivenWeHaveMarketData();
    GivenWeHaveAnInvalidOrder();
    
    EXPECT_EQ(cut.checkSlippage(order, marketData), false);
}

TEST_F(OrderValidatorTests, GivenWeHaveValidPositionSize_WeValidateCorrectly)
{
    GivenWeHaveMarketData();
    GivenWeHaveABuyOrder();
    
    EXPECT_EQ(cut.checkMaxPositionSize(order, 0), true);
}

TEST_F(OrderValidatorTests, GivenWeHaveInvalidPositionSize_WeValidateCorrectly)
{
    GivenWeHaveMarketData();
    GivenWeHaveAnInvalidOrder();   // quantity 20, above the limit of 10

    EXPECT_EQ(cut.checkMaxPositionSize(order, 0), false);
}

TEST_F(OrderValidatorTests, OrdersThatShrinkThePositionAreAlwaysAllowed)
{
    GivenWeHaveMarketData();
    Order sell(OrderType::SELL, "AAPL", 1, 109.0);
    Order buy(OrderType::BUY, "AAPL", 1, 109.0);

    EXPECT_TRUE(cut.checkMaxPositionSize(sell, 11));    // over the limit long: selling reduces it
    EXPECT_FALSE(cut.checkMaxPositionSize(buy, 11));    // buying would make it worse
    EXPECT_TRUE(cut.checkMaxPositionSize(buy, -11));    // over the limit short: buying reduces it
    EXPECT_FALSE(cut.checkMaxPositionSize(sell, -11));
}

TEST_F(OrderValidatorTests, ShortsAreLimitedToo)
{
    GivenWeHaveMarketData();
    Order sell(OrderType::SELL, "AAPL", 1, 109.0);
    EXPECT_TRUE(cut.checkMaxPositionSize(sell, -9));    // to -10: at the limit
    EXPECT_FALSE(cut.checkMaxPositionSize(sell, -10));  // to -11: beyond it
}

TEST_F(OrderValidatorTests, GivenWeHaveAValidOrder_WeValidateCorrectly)
{
    GivenWeHaveMarketData();
    GivenWeHaveABuyOrder();
    order.setStopLoss(10);
    order.setTakeProfit(10);
    
    EXPECT_EQ(cut.validateOrder(order, marketData, positions), true);
}

TEST_F(OrderValidatorTests, GivenWeHaveAnInvalidOrder_WeValidateCorrectly)
{
    GivenWeHaveMarketData();
    GivenWeHaveAnInvalidOrder();
    
    EXPECT_EQ(cut.validateOrder(order, marketData, positions), false);
}
TEST(OrderValidatorCloseTests, AnOrderThatOnlyClosesIsNeverTooBig)
{
    Order sellAll(OrderType::SELL, "PEP", 20, 100), sellMore(OrderType::SELL, "PEP", 25, 100),
          buyMore(OrderType::BUY, "PEP", 20, 100);
    EXPECT_TRUE(OrderValidator::onlyCloses(sellAll, 20));
    EXPECT_FALSE(OrderValidator::onlyCloses(sellMore, 20));   // would go short
    EXPECT_FALSE(OrderValidator::onlyCloses(buyMore, 20));    // adds
    EXPECT_TRUE(OrderValidator::onlyCloses(buyMore, -20));    // covers a short
}
