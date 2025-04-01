#include <iostream>
#include <unistd.h> 
#include "../src/data_access/MarketData.hpp"
#include "../src/strategy_engine/StrategyEngine.hpp"
#include "../src/strategy_engine/StrategyFactory.hpp"
#include "../src/broker/SimulatedBroker.hpp"

#define MIN_PER_RUN 1

using namespace std;

int main()
{
    Config config;
    json algoConfig = config.loadConfig();
    MarketData marketData;
    // marketData.process(algoConfig);
    StrategyFactory stratFactory(algoConfig);
    StrategyEngine stratEngine;
    SimulatedBroker broker(marketData); // Change this to IBKR

    stratEngine.setUp(algoConfig, stratFactory, marketData, &broker);
    while (true)
    {
        stratEngine.run();
        sleep(60*MIN_PER_RUN);
    }
    return 0;
}