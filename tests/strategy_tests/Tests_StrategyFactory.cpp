#include <gtest/gtest.h>
#include "../../src/strategy_engine/StrategyFactory.hpp"

class StrategyFactoryTests : public ::testing::Test {
public:
    StrategyFactory* cut;

    void SetUp() override 
    {
        cut = new StrategyFactory();
    }

    void TearDown() override 
    {
        delete cut;
    }
};

TEST_F(StrategyFactoryTests, CannBeInstantiated)
{
    StrategyFactory cut;
}

TEST_F(StrategyFactoryTests, CanLoadJsonCorrectly)
{
    cut->loadJson("/Users/james/Projects/C++AlgoTrader/tests/strategy_tests/test_data/strategies.json");
    
}