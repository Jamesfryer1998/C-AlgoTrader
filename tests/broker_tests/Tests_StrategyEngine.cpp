#include <gtest/gtest.h>
#include "../../src/broker/SimulatedBroker.hpp"


class SimulatedBrokerTests : public ::testing::Test {
    public:
        
        void SetUp() override 
        {

        }

        void TearDown() override 
        {

        }

        MarketData marketData;


};

TEST_F(SimulatedBrokerTests, CanInstantiate)
{
    SimulatedBroker cut(marketData);
}

TEST_F(SimulatedBrokerTests, CanProcess)
{
    SimulatedBroker cut(marketData);
    cut.process();
}
