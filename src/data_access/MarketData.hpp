#pragma once
#include "CSVParser.hpp"
#include "../util/Config.hpp"

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
         * Append one new bar (used by live trading).
         * @param bar The newest market condition
         * @param maxBars Keep at least the newest maxBars bars, trimming in batches (0 = keep everything)
         */
        void addBar(const MarketCondition& bar, size_t maxBars = 0);
        
        /**
         * Get all market data
         * @return Vector of market conditions
         */
        const std::vector<MarketCondition>& getData() const;
        
        /**
         * Get the last (most recent) close price
         * @return Close price as float
         */
        float getLastClosePrice() const;
        
        /**
         * Get the most recent market condition
         * @return Most recent market condition
         */
        MarketCondition getCurrentData() const;

    private:
        std::vector<MarketCondition> data;
};