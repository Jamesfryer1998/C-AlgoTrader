#include <iostream>
#include <unistd.h> 
#include "../src/data_access/MarketData.hpp"
#include "../src/strategy_engine/StrategyEngine.hpp"

#define MIN_PER_RUN 1

using namespace std;

int main()
{
    MarketData marketData;
    StrategyEngine stratEngine;

    stratEngine.setUp();
    stratEngine.inputMarketData(marketData);
    while (true)
    {
        stratEngine.run();
        sleep(60*MIN_PER_RUN);
    }

    return 0;
}