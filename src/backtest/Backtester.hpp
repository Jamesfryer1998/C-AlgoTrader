#include <iostream>
#include <vector>
#include <unistd.h>
#include "../data_access/MarketData.hpp"
#include "../strategy_engine/StrategyEngine.hpp"
#include "../strategy_engine/StrategyFactory.hpp"
#include "../broker/SimulatedBroker.hpp"

class Backtester {
public:
    Backtester(const json& algoConfig);

    void run();

private:
    MarketData marketData;
    SimulatedBroker broker;
    StrategyFactory stratFactory;
    StrategyEngine stratEngine;
    
    void logPerformance();
    void printReport();
};