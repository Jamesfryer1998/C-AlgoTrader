#include "Backtester.hpp"

Backtester::Backtester(const json& algoConfig)
        : marketData(), broker(marketData), stratFactory(), stratEngine() 
{
    marketData.process(algoConfig);
    stratEngine.setUp(algoConfig, stratFactory, marketData, &broker);
}

void 
Backtester::run() 
{
    marketData.rewind(); // Start at the beginning of historical data
    while (marketData.hasNext()) { // Ensure historical data exists
        marketData.next();  // Move to the next timestep
        stratEngine.run();  // Execute strategy
        broker.nextStep();  // Simulate trade execution
        logPerformance();   // Collect performance data
    }
    printReport();
}

void 
Backtester::logPerformance() 
{
    // Example: track profit/loss (implement details in SimulatedBroker)
    // std::cout << "Current PnL: " << broker.getPnL() << "\n";
}

void 
Backtester::printReport() 
{
    // std::cout << "Backtest complete. Final PnL: " << broker.getPnL() << "\n";
}