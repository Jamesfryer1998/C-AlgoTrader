#include <gtest/gtest.h>
#include "../../src/data_access/MarketData.hpp"


class MarketDataTests : public ::testing::Test 
{
    public:

        MarketData cut;
        Config config;
        string dataFilePath = config.getTestPath("data_access_tests/test_data/market_data_test_1.csv");

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
    EXPECT_EQ(data.front().DateTime, "2025-02-08");
    EXPECT_EQ(data.front().Close, 100);
    EXPECT_EQ(data.back().DateTime, "2025-02-17");
    EXPECT_EQ(data.back().Close, 109);
    EXPECT_EQ(data.size(), 10);
}

TEST_F(MarketDataTests, CanUpdateMarketDataFromNewFileCorrectly)
{
    string dataFilePath2 = config.getTestPath("data_access_tests/test_data/market_data_test_2.csv");
    cut.loadData(dataFilePath);
    cut.loadData(dataFilePath2);
    auto data = cut.getData();
    EXPECT_EQ(data.front().DateTime, "2025-02-18");
    EXPECT_EQ(data.front().Close, 100);
    EXPECT_EQ(data.back().DateTime, "2025-02-27");
    EXPECT_EQ(data.back().Close, 109);
    EXPECT_EQ(data.size(), 10);
}

TEST_F(MarketDataTests, CanGetTheLastClosePrice)
{
    cut.loadData(dataFilePath);
    EXPECT_EQ(cut.getLastClosePrice(), 109);
}