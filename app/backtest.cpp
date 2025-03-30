#include <iostream>
#include <unistd.h> 
#include "../src/backtest/Backtester.hpp"


int backtest() {
    Config config;
    json algoConfig = config.loadConfig();
    Backtester backtester(algoConfig);
    backtester.run();
    return 0;
}