#include <gtest/gtest.h>
#include "../../src/strategy_engine/StrategyFactory.hpp"


// Mock Strategies for Testing
class TestStrategy1 : public StrategyBase {
public:
    void execute() override {}
    static bool registered;
};
bool TestStrategy1::registered = [] {
    StrategyBase::registerStrategy("test_strategy_1", []() { return std::make_unique<TestStrategy1>(); });
    return true;
}();

class TestStrategy2 : public StrategyBase {
public:
    void execute() override {}
    static bool registered;
};
bool TestStrategy2::registered = [] {
    StrategyBase::registerStrategy("test_strategy_2", []() { return std::make_unique<TestStrategy2>(); });
    return true;
}();

class TestStrategy3 : public StrategyBase {
public:
    void execute() override {}
    static bool registered;
};
bool TestStrategy3::registered = [] {
    StrategyBase::registerStrategy("test_strategy_3", []() { return std::make_unique<TestStrategy3>(); });
    return true;
}();

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
    EXPECT_EQ("test_strategy_1", cut.getJson()["strategies"][0]["name"]);    
    EXPECT_EQ("test_strategy_2", cut.getJson()["strategies"][1]["name"]);    
    EXPECT_EQ("test_strategy_3", cut.getJson()["strategies"][2]["name"]);    
}

TEST_F(StrategyFactoryTests, StrategyFactory_GenerateStrategies) 
{
    auto strats = cut.generateStrategies();

    // Check number of strategies created
    ASSERT_EQ(strats.size(), 3);

    // Check the types of strategies
    EXPECT_NE(dynamic_cast<TestStrategy1*>(strats[0].get()), nullptr);
    EXPECT_NE(dynamic_cast<TestStrategy2*>(strats[1].get()), nullptr);
    EXPECT_NE(dynamic_cast<TestStrategy3*>(strats[2].get()), nullptr);
}