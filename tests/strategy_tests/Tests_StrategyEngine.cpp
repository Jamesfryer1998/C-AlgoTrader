#include <gtest/gtest.h>
#include "../../src/strategy_engine/StrategyEngine.hpp"
#include "../../src/broker/SimulatedBroker.hpp"


class StrategyEngineTests : public ::testing::Test {
public:

    StrategyEngine cut;
    Config config;

    void SetUp() override 
    {
        config.loadJson(config.getTestPath("strategy_tests/test_data/config_test.json"));
        jsonConfig = config.loadConfig();
    }

    void TearDown() override 
    {

    }

    json jsonConfig;
};

TEST_F(StrategyEngineTests, CanInstantiate)
{
    StrategyEngine cut;
}

TEST_F(StrategyEngineTests, CanSetUpStrategyEngine)
{
    // Use consolidated config file
    string stratFilePath = config.getTestPath("strategy_tests/test_data/strategies_consolidated.json");
    MarketData marketData;
    StrategyFactory stratFactory(stratFilePath);
    SimulatedBroker broker(marketData);
    cut.setUp(jsonConfig, stratFactory, marketData, &broker);
}


// When it comes to testing strat engine correclty makes orders

// TEST_F(StrategyEngineTests, CanGetOmsFromStrategyEngine)
// {
//     string stratFilePath = config.getTestPath("strategy_tests/test_data/strategies_consolidated.json");
//     MarketData marketData;
//     StrategyFactory stratFactory(stratFilePath);
//     SimulatedBroker broker(marketData);
//     cut.setUp(jsonConfig, stratFactory, marketData, &broker);
//     auto oms = cut.getOms();
// }

// ---------- Strategies must agree before an order is sent ----------

TEST(StrategyConsensusTests, NoProposalsMeansNoOrder)
{
    Order chosen;
    EXPECT_FALSE(StrategyEngine::chooseOrder({}, chosen));
}

TEST(StrategyConsensusTests, SingleStrategyOrderIsSent)
{
    Order chosen;
    std::vector<Order> proposals = {Order(OrderType::BUY, "NVDA", 1, 100)};
    ASSERT_TRUE(StrategyEngine::chooseOrder(proposals, chosen));
    EXPECT_EQ(chosen.getTypeAsString(), "BUY");
}

TEST(StrategyConsensusTests, AgreeingStrategiesSendOneOrder)
{
    Order chosen;
    std::vector<Order> proposals = {Order(OrderType::SELL, "NVDA", 1, 100), Order(OrderType::SELL, "NVDA", 2, 101)};
    ASSERT_TRUE(StrategyEngine::chooseOrder(proposals, chosen));
    EXPECT_EQ(chosen.getTypeAsString(), "SELL");
    EXPECT_FLOAT_EQ(chosen.getQuantity(), 1); // the first strategy's order, sent once
}

TEST(StrategyConsensusTests, DisagreeingStrategiesSendNothing)
{
    Order chosen;
    std::vector<Order> proposals = {Order(OrderType::BUY, "NVDA", 1, 100), Order(OrderType::SELL, "NVDA", 1, 100)};
    EXPECT_FALSE(StrategyEngine::chooseOrder(proposals, chosen));
}

TEST(StrategyConsensusTests, LimitBuyAgreesWithMarketBuy)
{
    Order chosen;
    std::vector<Order> proposals = {Order(OrderType::BUY, "NVDA", 1, 100), Order(OrderType::LIMIT_BUY, "NVDA", 1, 99)};
    EXPECT_TRUE(StrategyEngine::chooseOrder(proposals, chosen));
}
