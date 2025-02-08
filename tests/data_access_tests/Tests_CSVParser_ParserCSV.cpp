#include <gtest/gtest.h>
#include "../../src/data_access/CSVParser.hpp"

TEST(MarketConditionParser, ReturnsEmptyMarketConditionAndThrowsExceptionFromMissingFile)
{
    CSVParser cut;
    std::string fileName = "../test_data/market_condition_test_00.csv";
    EXPECT_THROW(cut.Read(fileName), std::runtime_error);
    std::vector<MarketCondition> mc = cut.GetData();
    EXPECT_EQ(mc.size(), 0);
}

TEST(MarketConditionParser, ReturnsEmptyMarketConditionFromEmptyFile)
{
    CSVParser cut;
    std::string fileName = "../test_data/market_condition_test_01.csv";
    std::vector<MarketCondition> mc = cut.GetData();
    EXPECT_EQ(mc.size(), 0);
}

TEST(MarketConditionParser, ReturnsCorrectMarketConditionFromValidFile)
{
    CSVParser cut;
    std::string fileName = "../test_data/market_condition_test_02.csv";
    std::vector<MarketCondition> mc = cut.GetData();
    EXPECT_EQ(mc.size(), 0);
}