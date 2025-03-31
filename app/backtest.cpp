#include <iostream>
#include <unistd.h> 
#include "../src/backtest/Backtester.hpp"


int main() {
    Config config;
    json algoConfig = config.loadConfig();
    Backtester backtester(algoConfig);
    backtester.run();
    return 0;
}

// How to run this?