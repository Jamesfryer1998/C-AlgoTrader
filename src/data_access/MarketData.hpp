#pragma once
#include "CSVParser.hpp"
#include "DataStitcher.hpp"
#include "../util/Config.hpp"
#include <chrono>
#include <future>
#include <thread>

using namespace std;

/**
 * MarketData
 * 
 * Responsible for loading, processing, and providing access to market data.
 * This class is designed to be used by both live trading and backtesting systems.
 */
class MarketData
{
    public:
        MarketData();
        ~MarketData();
        
        /**
         * Process market data from default sources
         * @param configData Configuration for data processing
         */
        void process(json configData);
        
        /**
         * Process market data for a specific date range
         * @param configData Configuration for data processing
         * @param startDate Start date for the data range
         * @param endDate End date for the data range
         */
        void processForDateRange(json configData, const std::string& startDate, const std::string& endDate);
        
        /**
         * Process market data in parallel for improved performance
         * @param configData Configuration for data processing
         * @param numThreads Number of threads to use (0 = auto)
         */
        void processParallel(json configData, int numThreads = 0);
        
        /**
         * Load data from a specific file
         * @param filePath Path to the data file
         */
        void loadData(const std::string& filePath);
        
        /**
         * Update the market data with new data
         * @param marketData New market data
         */
        void update(std::vector<MarketCondition>& marketData);
        
        /**
         * Get all market data
         * @return Vector of market conditions
         */
        std::vector<MarketCondition> getData() const;
        
        /**
         * Generate file path for data based on configuration
         * @param configData Configuration data
         * @return File path string
         */
        string generateFilePath(json configData);
        
        /**
         * Get project root directory
         * @return Path to project root
         */
        string getProjectRoot();
        
        /**
         * Get the last (most recent) close price
         * @return Close price as float
         */
        float getLastClosePrice();
        
        /**
         * Get the most recent market condition
         * @return Most recent market condition
         */
        MarketCondition getCurrentData();

    private:
        std::vector<MarketCondition> data;
        
        // Helper methods
        std::string getDataDirectory();
};