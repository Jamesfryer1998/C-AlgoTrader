#include <iostream>
#include "../src/data_access/MarketData.hpp"
#include "../src/strategy_engine/StrategyEngine.hpp"

int main()
{
    std::cout << "This is a test line" << std::endl;
    MarketData marketData;
    StrategyEngine stratEngine;

    stratEngine.setUp();
    stratEngine.inputMarketData(marketData);
    stratEngine.run();

    return 0;
}