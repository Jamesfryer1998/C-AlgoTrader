#include <gtest/gtest.h>
#include "../../src/data_access/MarketData.hpp"


class MarketDataTests : public ::testing::Test 
{
    public:

        MarketData cut;
        Config config;
        string dataFilePath = "/Users/james/Projects/C++AlgoTrader/tests/data_access_tests/test_data/market_data_test.csv";

        void SetUp() override 
        {

        }

        void TearDown() override 
        {

        }
};

TEST_F(MarketDataTests, CanBeInstigated)
{
    MarketData cut;
}

TEST_F(MarketDataTests, DataCanBeLoaded)
{
    cut.loadData(dataFilePath);
    EXPECT_EQ(10, cut.getData().size());
}

TEST_F(MarketDataTests, DataLoadedInCorrectOrder)
{
    cut.loadData(dataFilePath);
    auto data = cut.getData();
    EXPECT_EQ(data[0].DateTime, "2025-02-08");
    EXPECT_EQ(data[-1].DateTime, "2025-02-17");
}