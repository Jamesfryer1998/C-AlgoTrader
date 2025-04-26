#include <gtest/gtest.h>
#include "../../src/strategy_engine/StrategyFactory.hpp"

class StrategyFactoryTests : public ::testing::Test {
public:
    Config config;
    string filePath = config.getTestPath("strategy_tests/test_data/strategies_consolidated.json");
    StrategyFactory cut{filePath};

    void SetUp() override 
    {

    }

    void TearDown() override 
    {

    }
};

TEST_F(StrategyFactoryTests, CannBeInstantiated)
{
    StrategyFactory cut{filePath};
}

TEST_F(StrategyFactoryTests, LoadsCorrectNumberOfStrategies)
{
    EXPECT_EQ(3, cut.getJson()["strategies"].size());    
}

TEST_F(StrategyFactoryTests, LoadsCorrectNameOfStrategiesFromJson)
{
    EXPECT_EQ("RSI", cut.getJson()["strategies"][0]["name"]);    
    EXPECT_EQ("MACD", cut.getJson()["strategies"][1]["name"]);    
    EXPECT_EQ("MEANREV", cut.getJson()["strategies"][2]["name"]);    
}

TEST_F(StrategyFactoryTests, StrategyFactory_GenerateStrategies) 
{
    auto strats = cut.generateStrategies();

    // Check number of strategies created
    ASSERT_EQ(strats.size(), 2);

    // Check the types of strategies
    EXPECT_EQ("RSI", strats[0].get()->_strategyAttribute.name);
    EXPECT_EQ("MACD", strats[1].get()->_strategyAttribute.name);
}