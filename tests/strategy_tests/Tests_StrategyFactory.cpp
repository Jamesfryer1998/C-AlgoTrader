#include <gtest/gtest.h>
#include "../../src/strategy_engine/StrategyFactory.hpp"

class StrategyFactoryTests : public ::testing::Test {
public:

    StrategyFactory cut{"/Users/james/Projects/C++AlgoTrader/tests/strategy_tests/test_data/strategies.json"};

    void SetUp() override 
    {

    }

    void TearDown() override 
    {

    }
};

TEST_F(StrategyFactoryTests, CannBeInstantiated)
{
    StrategyFactory cut{"/Users/james/Projects/C++AlgoTrader/tests/strategy_tests/test_data/strategies.json"};
}

TEST_F(StrategyFactoryTests, CanLoadJsonCorrectly)
{
    EXPECT_EQ(1, cut.getJson().size());    
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
    ASSERT_EQ(strats.size(), 3);

    // Check the types of strategies
    // EXPECT_EQ();
}