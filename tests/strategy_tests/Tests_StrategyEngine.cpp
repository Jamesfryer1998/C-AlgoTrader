#include <gtest/gtest.h>
#include "../../src/strategy_engine/StrategyEngine.hpp"
#include "../../src/broker/SimulatedBroker.hpp"


class StrategyEngineTests : public ::testing::Test {
public:

    StrategyEngine cut;
    Config config;

    void SetUp() override 
    {
        config.loadJson("/Users/james/Projects/C++AlgoTrader/tests/strategy_tests/test_data/config_test.json");
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
    string stratFilePath = "/Users/james/Projects/C++AlgoTrader/tests/strategy_tests/test_data/strategies.json";
    MarketData marketData;
    StrategyFactory stratFactory(stratFilePath);
    SimulatedBroker broker(marketData);
    cut.setUp(jsonConfig, stratFactory, marketData, &broker);
}


// When it comes to testing strat engine correclty makes orders

// TEST_F(StrategyEngineTests, CanSetUpStrategyEngine)
// {
//     string stratFilePath = "/Users/james/Projects/C++AlgoTrader/tests/strategy_tests/test_data/strategies.json";
//     MarketData marketData;
//     StrategyFactory stratFactory(stratFilePath);
//     cut.setUp(jsonConfig, stratFactory, marketData);
//     auto oms = cut.getOms();
// }
