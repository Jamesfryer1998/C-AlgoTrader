#include <iostream>
#include <unistd.h>
#include <string>
#include "../src/backtest/Backtester.hpp"
#include "../src/util/Config.hpp"

void printUsage() {
    std::cout << "AlgoTrader Backtester" << std::endl;
    std::cout << "---------------------" << std::endl;
    std::cout << "A backtesting system for analyzing trading strategies" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --capital <amount>       Set starting capital (default: $100,000)" << std::endl;
    std::cout << "  --commission <amount>    Set commission per trade (default: $1.00)" << std::endl;
    std::cout << "  --slippage <percentage>  Set slippage percentage (default: 0.05%)" << std::endl;
    std::cout << "  --detailed               Enable detailed logging during backtest" << std::endl;
    std::cout << "  --output <filename>      Save results to CSV file" << std::endl;
    std::cout << "  --help                   Display this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    // Default settings
    double startingCapital = 100000.0;
    double commission = 1.0;
    double slippage = 0.0005;
    bool detailedLogging = false;
    std::string outputFile = "";
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            printUsage();
            return 0;
        } else if (arg == "--capital" && i + 1 < argc) {
            startingCapital = std::stod(argv[++i]);
        } else if (arg == "--commission" && i + 1 < argc) {
            commission = std::stod(argv[++i]);
        } else if (arg == "--slippage" && i + 1 < argc) {
            slippage = std::stod(argv[++i]) / 100.0; // Convert from percentage
        } else if (arg == "--detailed") {
            detailedLogging = true;
        } else if (arg == "--output" && i + 1 < argc) {
            outputFile = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printUsage();
            return 1;
        }
    }
    
    try {
        // Load configuration
        Config config;
        json algoConfig = config.loadConfig();
        
        // Create and configure backtester
        Backtester backtester(algoConfig);
        backtester.setStartingCapital(startingCapital);
        backtester.setCommissionPerTrade(commission);
        backtester.setSlippagePercentage(slippage);
        backtester.enableDetailedLogging(detailedLogging);
        
        if (!outputFile.empty()) {
            backtester.saveResultsToFile(outputFile);
        }
        
        // Run backtest
        std::cout << "Starting backtest with:" << std::endl;
        std::cout << "- Starting capital: $" << startingCapital << std::endl;
        std::cout << "- Commission per trade: $" << commission << std::endl;
        std::cout << "- Slippage: " << (slippage * 100.0) << "%" << std::endl;
        
        backtester.run();
        
        // Access performance metrics if needed
        // const PerformanceMetrics& metrics = backtester.getPerformanceMetrics();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}