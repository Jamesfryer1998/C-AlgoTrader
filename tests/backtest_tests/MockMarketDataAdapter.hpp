#pragma once

#include <memory>
#include <vector>
#include "../../src/data_access/MarketData.hpp"
#include "../../src/backtest/BacktestMarketDataAdapter.hpp"

/**
 * MockMarketDataAdapter
 * 
 * A test helper class that combines MarketData and BacktestMarketDataAdapter
 * to make it easier to set up test data for backtesting.
 */
class MockMarketDataAdapter {
public:
    MockMarketDataAdapter() {
        marketData = std::make_unique<MarketData>();
        adapter.initialize(*marketData);
    }
    
    /**
     * Set mock data for testing
     * @param mockData Vector of market conditions to use as test data
     */
    void setMockData(std::vector<MarketCondition>& mockData) {
        // Verify data is not empty
        if (mockData.empty()) {
            std::cerr << "ERROR: Attempted to set empty mock data in MockMarketDataAdapter::setMockData!" << std::endl;
            return;
        }
        
        std::cout << "MockMarketDataAdapter: Setting " << mockData.size() << " data points" << std::endl;
        
        // Load the mock data into the adapter
        adapter.loadMockData(mockData);
        
        std::cout << "MockMarketDataAdapter: After update, data size=" << adapter.getDataSize() 
                  << ", currentIndex=" << adapter.getCurrentIndex() << std::endl;
    }
    
    /**
     * Create mock market data for testing
     * @param numDays Number of days of data to create
     * @param startPrice Starting price for the data
     * @return Vector of market conditions
     */
    std::vector<MarketCondition> createMockData(int numDays = 10, float startPrice = 100.0f) {
        std::vector<MarketCondition> mockData;
        
        // Ensure we're creating at least one data point
        numDays = std::max(1, numDays);
        
        for (int i = 0; i < numDays; i++) {
            std::string dateTime = "2025-03-" + std::to_string(20 + i) + " 10:00:00";
            float price = startPrice + (i * 2.0f); // Price increases by $2 each day
            
            MarketCondition condition(
                dateTime,          // DateTime
                "AAPL",           // Ticker
                price,            // Open
                price,            // Close
                1000 * (i + 1),   // Volume
                "1m"              // TimeInterval
            );
            
            mockData.push_back(condition);
        }
        
        std::cout << "MockMarketDataAdapter: Created " << mockData.size() << " data points" << std::endl;
        return mockData;
    }
    
    /**
     * Set up mock data for testing
     * @param numDays Number of days of data to create
     */
    void setupMockDataForTesting(int numDays = 10) {
        auto mockData = createMockData(numDays);
        
        // Ensure we have created data
        if (mockData.empty()) {
            std::cerr << "ERROR: Failed to create mock data in setupMockDataForTesting!" << std::endl;
            // Create backup data
            mockData.push_back(MarketCondition(
                "2025-03-20 10:00:00",  // DateTime
                "AAPL",                 // Ticker
                100.0f,                 // Open
                100.0f,                 // Close
                1000,                   // Volume
                "1m"                    // TimeInterval
            ));
        }
        
        setMockData(mockData);
    }
    
    /**
     * Get the underlying MarketData instance
     * @return Reference to the underlying MarketData instance
     */
    MarketData& getMarketData() {
        return *marketData;
    }
    
    /**
     * Get the underlying BacktestMarketDataAdapter
     * @return Reference to the underlying BacktestMarketDataAdapter
     */
    BacktestMarketDataAdapter& getAdapter() {
        return adapter;
    }
    
    /**
     * Get the full dataset for backtesting
     * @return Vector of market conditions with all data points
     */
    std::vector<MarketCondition> getFullDataset() {
        std::vector<MarketCondition> result;
        size_t dataSize = adapter.getDataSize();
        
        if (dataSize == 0) {
            return result;
        }
        
        // Save current index
        int savedIndex = adapter.getCurrentIndex();
        
        // Rewind to beginning
        adapter.rewind();
        
        // Collect all data points
        for (size_t i = 0; i < dataSize; i++) {
            result.push_back(adapter.getCurrentData());
            if (adapter.hasNext()) {
                adapter.next();
            }
        }
        
        // Restore to original position
        adapter.rewind();
        for (int i = 0; i < savedIndex; i++) {
            if (adapter.hasNext()) {
                adapter.next();
            }
        }
        
        return result;
    }
    
private:
    std::unique_ptr<MarketData> marketData;
    BacktestMarketDataAdapter adapter;
};