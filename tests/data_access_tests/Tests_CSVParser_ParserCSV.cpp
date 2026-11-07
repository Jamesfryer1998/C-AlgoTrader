#include <gtest/gtest.h>
#include "../../src/data_access/CSVParser.hpp"
#include "../../src/util/Config.hpp"

TEST(MarketConditionParser, ReturnsEmptyMarketConditionAndThrowsExceptionFromMissingFile)
{
    CSVParser cut;
    EXPECT_THROW(cut.Read("test_data/market_condition_test_00.csv"), std::runtime_error);   // no such file
    std::vector<MarketCondition> mc = cut.GetData();
    EXPECT_EQ(mc.size(), 0);
}

TEST(MarketConditionParser, ReturnsEmptyMarketConditionFromEmptyFile)
{
    Config config;
    CSVParser cut;
    cut.Read(config.getTestPath("data_access_tests/test_data/market_condition_test_01.csv"));

    std::vector<MarketCondition> mc = cut.GetData();
    EXPECT_EQ(mc.size(), 0);
}

TEST(MarketConditionParser, ReturnsCorrectMarketConditionFromValidFile)
{
    Config config;
    CSVParser cut;
    cut.Read(config.getTestPath("data_access_tests/test_data/market_condition_test_02.csv"));

    std::vector<MarketCondition> mc = cut.GetData();
    ASSERT_EQ(mc.size(), 1u);
    EXPECT_EQ(mc[0].Ticker, "AAPL");
    EXPECT_FLOAT_EQ(mc[0].Close, 100);
}
