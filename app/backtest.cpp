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
    std::cout << "  --start-date <YYYY-MM-DD> Start date for backtest (default: 7 days ago)" << std::endl;
    std::cout << "  --end-date <YYYY-MM-DD>  End date for backtest (default: today)" << std::endl;
    std::cout << "  --threads <num>          Number of threads to use (default: all available cores)" << std::endl;
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
    std::string startDate = "";
    std::string endDate = "";
    int numThreads = 0;  // 0 means use all available cores
    
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
        } else if (arg == "--start-date" && i + 1 < argc) {
            startDate = argv[++i];
        } else if (arg == "--end-date" && i + 1 < argc) {
            endDate = argv[++i];
        } else if (arg == "--threads" && i + 1 < argc) {
            numThreads = std::stoi(argv[++i]);
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
        backtester.setNumThreads(numThreads);
        
        // // Set date range if provided
        // if (!startDate.empty() && !endDate.empty()) {
        //     backtester.setDateRange(startDate, endDate);
        // } else if (!startDate.empty()) {
        //     // Get current date for end date
        //     auto now = std::chrono::system_clock::now();
        //     auto timeT = std::chrono::system_clock::to_time_t(now);
        //     std::stringstream ss;
        //     ss << std::put_time(std::localtime(&timeT), "%Y-%m-%d");
        //     backtester.setDateRange(startDate, ss.str());
        // } else if (!endDate.empty()) {
        //     // Calculate start date (7 days before end date)
        //     std::tm endTm = {};
        //     std::istringstream endDateStream(endDate);
        //     endDateStream >> std::get_time(&endTm, "%Y-%m-%d");
        //     auto endTimePoint = std::chrono::system_clock::from_time_t(std::mktime(&endTm));
        //     auto startTimePoint = endTimePoint - std::chrono::hours(24 * 7);
        //     auto startTimeT = std::chrono::system_clock::to_time_t(startTimePoint);
        //     std::stringstream ss;
        //     ss << std::put_time(std::localtime(&startTimeT), "%Y-%m-%d");
        //     backtester.setDateRange(ss.str(), endDate);
        // }
        
        if (!outputFile.empty()) {
            backtester.saveResultsToFile(outputFile);
        }
        
        // Run backtest
        std::cout << "Starting backtest with:" << std::endl;
        std::cout << "- Starting capital: $" << startingCapital << std::endl;
        std::cout << "- Commission per trade: $" << commission << std::endl;
        std::cout << "- Slippage: " << (slippage * 100.0) << "%" << std::endl;
        std::cout << "- Threads: " << (numThreads > 0 ? std::to_string(numThreads) : "all available") << std::endl;
        
        backtester.run();
        
        // Access performance metrics
        const PerformanceMetrics& metrics = backtester.getPerformanceMetrics();
        
        // Display key performance metrics
        std::cout << "\nSummary:" << std::endl;
        std::cout << "- Total PnL: $" << metrics.totalPnL 
                  << " (" << metrics.totalPnLPercent << "%)" << std::endl;
        std::cout << "- Sharpe Ratio: " << metrics.sharpeRatio << std::endl;
        std::cout << "- Max Drawdown: " << metrics.maxDrawdownPercent << "%" << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}