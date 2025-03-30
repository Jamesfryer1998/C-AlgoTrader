#include <gtest/gtest.h>
#include "../../src/broker/SimulatedBroker.hpp"


class SimulatedBrokerTests : public ::testing::Test {
    public:
        
        Config config;

        void SetUp() override 
        {
            config.loadJson("/Users/james/Projects/C++AlgoTrader/tests/strategy_tests/test_data/config_test.json");
            jsonConfig = config.loadConfig();
        }

        void TearDown() override 
        {

        }

        MarketData marketData;
        json jsonConfig;
        string dataFilePath = "/Users/james/Projects/C++AlgoTrader/tests/data_access_tests/test_data/market_data_test_1.csv";


};

TEST_F(SimulatedBrokerTests, CanInstantiate)
{
    SimulatedBroker cut(marketData);
}

TEST_F(SimulatedBrokerTests, CanProcess)
{
    marketData.loadData(dataFilePath);
    SimulatedBroker cut(marketData);
    cut.process();
}

TEST_F(SimulatedBrokerTests, CanNextStep)
{
    marketData.loadData(dataFilePath);
    SimulatedBroker cut(marketData);
    cut.nextStep();
}