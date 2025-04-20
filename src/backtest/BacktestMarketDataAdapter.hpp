#pragma once

#include <vector>
#include <memory>
#include "../data_access/MarketData.hpp"

/**
 * BacktestMarketDataAdapter
 * 
 * This adapter wraps a MarketData instance and manages its state for backtesting.
 * It handles the time-stepping logic and data indexing without requiring modifications
 * to the core MarketData class.
 */
class BacktestMarketDataAdapter {
public:
    BacktestMarketDataAdapter();
    ~BacktestMarketDataAdapter();
    
    /**
     * Initialize the adapter with a MarketData instance
     * @param marketDataInstance The raw MarketData instance to adapt for backtesting
     */
    void initialize(MarketData& marketDataInstance);
    
    /**
     * Load historical data for backtesting
     * @param configData Configuration for data loading
     * @param startDate Start date for backtest data range
     * @param endDate End date for backtest data range
     */
    void loadHistoricalData(const json& configData, const std::string& startDate, const std::string& endDate);
    
    /**
     * Load parallel historical data for backtesting (multi-threaded)
     * @param configData Configuration for data loading
     * @param numThreads Number of threads to use for processing
     */
    void loadHistoricalDataParallel(const json& configData, int numThreads);
    
    /**
     * Load mock data for backtesting (primarily for testing)
     * @param mockData Vector of market conditions to use for backtesting
     */
    void loadMockData(const std::vector<MarketCondition>& mockData);
    
    /**
     * Check if there are more data points available
     * @return true if more data is available, false otherwise
     */
    bool hasNext() const;
    
    /**
     * Advance to the next data point
     * This is the core time-stepping function for backtesting
     */
    void next();
    
    /**
     * Reset to the beginning of the data set
     */
    void rewind();
    
    /**
     * Get the current index in the dataset
     * @return Current index position
     */
    int getCurrentIndex() const;
    
    /**
     * Get the total size of the dataset
     * @return Total number of data points
     */
    size_t getDataSize() const;
    
    /**
     * Get the current data point without advancing
     * @return Current market condition
     */
    MarketCondition getCurrentData() const;
    
    /**
     * Check if we've reached the end of the dataset
     * @return true if at the last data point, false otherwise
     */
    bool reachedEnd() const;

private:
    MarketData* marketData;      // Reference to the wrapped MarketData instance
    std::vector<MarketCondition> fullDataset;  // Complete historical dataset
    int currentIndex;            // Current position in the dataset
    
    // Apply the current data point to the MarketData instance
    void updateMarketDataWithCurrentPoint();
    
    // Helper methods
    void validateMarketData() const;
};