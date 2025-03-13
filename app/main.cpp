#include <iostream>
#include "../src/data_access/MarketData.hpp"
#include "../src/strategy_engine/StrategyEngine.hpp"

int main()
{
    MarketData marketData;
    StrategyEngine stratEngine;

    stratEngine.setUp();
    stratEngine.inputMarketData(marketData);
    stratEngine.run();

    return 0;
}