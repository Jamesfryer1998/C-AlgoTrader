#include <iostream>
#include <unistd.h>
#include <string>
#include "../src/alpaca/AlpacaHistory.hpp"
#include "../src/backtest/Backtester.hpp"
#include "../src/util/Config.hpp"
#include "../src/util/DotEnv.hpp"
#include "../src/util/MarketTime.hpp"
#include "../src/util/Defaults.hpp"

namespace
{
    const double PERCENT = 100.0;
}

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
    std::cout << "  --download               Download the date range from Alpaca into data/ first (skips days already saved)" << std::endl;
    std::cout << "  --download-only          Download the date range from Alpaca and exit without backtesting" << std::endl;
    std::cout << "  --help                   Display this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    // Default settings
    double startingCapital = Defaults::STARTING_CAPITAL;
    double commission = Defaults::COMMISSION_PER_TRADE;
    double slippage = Defaults::SLIPPAGE;
    bool detailedLogging = false;
    std::string outputFile = "";
    std::string startDate = "";
    std::string endDate = "";
    int numThreads = 0;  // 0 means use all available cores
    bool download = false;
    bool downloadOnly = false;
    
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
        } else if (arg == "--download") {
            download = true;
        } else if (arg == "--download-only") {
            download = true;
            downloadOnly = true;
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
        
        // Date range: default is the last 7 days ending today
        std::time_t now = std::time(nullptr);
        if (endDate.empty())
            endDate = MarketTime::datePart(MarketTime::toNewYorkTime(now));
        if (startDate.empty())
            startDate = MarketTime::datePart(MarketTime::formatUtc(MarketTime::parseDate(endDate) - Defaults::BACKTEST_DAYS * MarketTime::SECONDS_PER_DAY));
        backtester.setDateRange(startDate, endDate);

        // Optionally fetch the data from Alpaca first
        if (download) {
            DotEnv::load(config.getProjectRoot() + "/.env");
            std::string dataDir = config.getProjectRoot() + "/" + algoConfig.value("marketDataBasePath", Defaults::DATA_FOLDER);
            AlpacaHistory history(algoConfig);
            if (history.downloadToCsv(startDate, endDate, dataDir) < 0) {
                std::cerr << "Download failed - check your keys in .env and the dates" << std::endl;
                return 1;
            }
            if (downloadOnly)
                return 0;
        }
        
        if (!outputFile.empty()) {
            backtester.saveResultsToFile(outputFile);
        }
        
        // Run backtest
        std::cout << "Starting backtest with:" << std::endl;
        std::cout << "- Starting capital: $" << startingCapital << std::endl;
        std::cout << "- Commission per trade: $" << commission << std::endl;
        std::cout << "- Slippage: " << (slippage * PERCENT) << "%" << std::endl;
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