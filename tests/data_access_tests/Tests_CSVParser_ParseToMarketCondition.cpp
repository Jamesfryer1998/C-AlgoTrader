#include <gtest/gtest.h>
#include "../../src/data_access/CSVParser.hpp"


TEST(MarketConditionParser, LineToMarketCondition)
{
    CSVParser cut;
    {
        std::string input = "1,2,3,4,5,6";
        MarketCondition condition = cut.ParseToMarketCondition(input);
        EXPECT_TRUE(condition.IsValid());
    }

    {
        std::string input = "1,2,3,4,5,6";
        MarketCondition condition = cut.ParseToMarketCondition(input);
        EXPECT_EQ(condition.DateTime, "1");
    }
}

TEST(MarketConditionParser, ThrowsCorrectException)
{
    CSVParser cut;
    {
        std::string input = "1,2,3,4";  // Only 5 tokens instead of 6
        EXPECT_THROW(cut.ParseToMarketCondition(input), std::runtime_error);
    }
}