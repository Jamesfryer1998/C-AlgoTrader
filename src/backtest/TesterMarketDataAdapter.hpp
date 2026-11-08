#pragma once

#include <vector>
#include <memory>
#include "../data_access/MarketData.hpp"

/**
 * TesterMarketDataAdapter
 * 
 * This adapter wraps a MarketData instance and manages its state for backtesting.
 * It handles the time-stepping logic and data indexing without requiring modifications
 * to the core MarketData class.
 */
class TesterMarketDataAdapter {
public:
    TesterMarketDataAdapter();
    ~TesterMarketDataAdapter();
    
    /**
     * Initialize the adapter with a MarketData instance
     * @param marketDataInstance The raw MarketData instance to adapt for backtesting
     */
    void initialize(MarketData& marketDataInstance);
    
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

    // Every bar loaded, oldest first
    const std::vector<MarketCondition>& getFullDataset() const { return fullDataset; }
    
    /**
     * Get the current data point without advancing
     * @return Current market condition
     */
    MarketCondition getCurrentData() const;

private:
    MarketData* marketData;      // Reference to the wrapped MarketData instance
    std::vector<MarketCondition> fullDataset;  // Complete historical dataset
    int currentIndex;            // Current position in the dataset
    
    // Helper methods
    void validateMarketData() const;
};