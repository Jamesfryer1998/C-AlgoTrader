#include <gtest/gtest.h>
#include "../../src/strategy_engine/StrategyEngine.hpp"


class StrategyEngineTests : public ::testing::Test {
public:

    StrategyEngine cut;
    Config config;
    string stratFilePath = "/Users/james/Projects/C++AlgoTrader/tests/strategy_tests/test_data/strategies.json";
    string configFilePath = "/Users/james/Projects/C++AlgoTrader/tests/strategy_tests/test_data/config_test.json";
    MarketData marketData;

    void SetUp() override 
    {
        config.loadJson(configFilePath);
        jsonConfig = config.loadConfig();
        StrategyFactory stratFactory(stratFilePath);
    }

    void TearDown() override 
    {

    }

    json jsonConfig;
    StrategyFactory stratFactory;
};

TEST_F(StrategyEngineTests, CanInstantiate)
{
    StrategyEngine cut;
}

TEST_F(StrategyEngineTests, CanSetUpStrategyEngine)
{
    cut.setUp(jsonConfig, stratFactory, marketData);
}


// When it comes to testing strat engine correclty makes orders

TEST_F(StrategyEngineTests, CanSetUpStrategyEngine)
{
    string stratFilePath = "/Users/james/Projects/C++AlgoTrader/tests/strategy_tests/test_data/strategies.json";
    MarketData marketData;
    StrategyFactory stratFactory(stratFilePath);
    cut.setUp(jsonConfig, stratFactory, marketData);
    auto oms = cut.getOms();
}
