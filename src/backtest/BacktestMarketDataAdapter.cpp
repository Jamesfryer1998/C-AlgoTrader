#include "BacktestMarketDataAdapter.hpp"
#include <stdexcept>
#include <iostream>

BacktestMarketDataAdapter::BacktestMarketDataAdapter()
    : marketData(nullptr), currentIndex(0)
{
}

BacktestMarketDataAdapter::~BacktestMarketDataAdapter()
{
    // We don't own the marketData pointer, so don't delete it
}

void 
BacktestMarketDataAdapter::initialize(MarketData& marketDataInstance)
{
    marketData = &marketDataInstance;
    currentIndex = 0;
}

void 
BacktestMarketDataAdapter::loadHistoricalData(const json& configData, const std::string& startDate, const std::string& endDate)
{
    validateMarketData();
    
    // Use the MarketData instance to load historical data
    marketData->processForDateRange(configData, startDate, endDate);
    
    // Store the full dataset for our own use
    fullDataset = marketData->getData();
    
    // Reset the index
    rewind();
    
    std::cout << "BacktestMarketDataAdapter: Loaded " << fullDataset.size() 
              << " historical data points from " << startDate << " to " << endDate << std::endl;
}

void 
BacktestMarketDataAdapter::loadHistoricalDataParallel(const json& configData, int numThreads)
{
    validateMarketData();
    
    // Use the MarketData instance to load historical data in parallel
    marketData->processParallel(configData, numThreads);
    
    // Store the full dataset for our own use
    fullDataset = marketData->getData();
    
    // Reset the index
    rewind();
    
    std::cout << "BacktestMarketDataAdapter: Loaded " << fullDataset.size() 
              << " historical data points using " << numThreads << " threads" << std::endl;
}

void 
BacktestMarketDataAdapter::loadMockData(const std::vector<MarketCondition>& mockData)
{
    validateMarketData();
    
    if (mockData.empty()) {
        throw std::runtime_error("Cannot load empty mock data");
    }
    
    // Update the MarketData instance with the mock data
    marketData->update(const_cast<std::vector<MarketCondition>&>(mockData));
    
    // Store the full dataset for our own use
    fullDataset = mockData;
    
    // Reset the index
    rewind();
    
    std::cout << "BacktestMarketDataAdapter: Loaded " << fullDataset.size() 
              << " mock data points" << std::endl;
}

bool 
BacktestMarketDataAdapter::hasNext() const
{
    return static_cast<size_t>(currentIndex) < fullDataset.size();
}

void 
BacktestMarketDataAdapter::next()
{
    if (!hasNext()) {
        return;
    }
    
    // Get the current market condition at the current index
    MarketCondition currentCondition = fullDataset[currentIndex];
    
    // Update the MarketData instance with the accumulated data up to the current index
    // This way strategies can access historical data points
    std::vector<MarketCondition> accumulatedData(fullDataset.begin(), fullDataset.begin() + currentIndex + 1);
    marketData->update(accumulatedData);
    
    // Advance the index
    std::cout << "DEBUG: Processing timepoint [" << currentCondition.DateTime 
              << "] at index " << currentIndex << " (data size: " << accumulatedData.size() << ")" << std::endl;
    currentIndex++;
}

void 
BacktestMarketDataAdapter::rewind()
{
    std::cout << "Rewinding to the beginning of the data" << std::endl;
    currentIndex = 0;
    
    // If we have data, update the MarketData instance with the first point
    if (!fullDataset.empty()) {
        // Update with the first data point only
        std::vector<MarketCondition> initialData = { fullDataset[0] };
        marketData->update(initialData);
    }
}

int 
BacktestMarketDataAdapter::getCurrentIndex() const
{
    return currentIndex;
}

size_t 
BacktestMarketDataAdapter::getDataSize() const
{
    return fullDataset.size();
}

MarketCondition 
BacktestMarketDataAdapter::getCurrentData() const
{
    validateMarketData();
    
    if (fullDataset.empty()) {
        throw std::runtime_error("No data available");
    }
    
    // If we're at the beginning, return the first data point
    if (currentIndex == 0 && !fullDataset.empty()) {
        return fullDataset[0];
    }
    
    // Return the data point at the previous index (the one we just processed)
    int idx = std::min(currentIndex - 1, static_cast<int>(fullDataset.size() - 1));
    return fullDataset[idx];
}

bool 
BacktestMarketDataAdapter::reachedEnd() const
{
    return static_cast<size_t>(currentIndex + 1) >= fullDataset.size();
}

void 
BacktestMarketDataAdapter::updateMarketDataWithCurrentPoint()
{
    validateMarketData();
    
    if (fullDataset.empty() || currentIndex >= static_cast<int>(fullDataset.size())) {
        return;
    }
    
    // Update with the current data point
    std::vector<MarketCondition> currentData = { fullDataset[currentIndex] };
    marketData->update(currentData);
}

void 
BacktestMarketDataAdapter::validateMarketData() const
{
    if (marketData == nullptr) {
        throw std::runtime_error("MarketData instance not initialized. Call initialize() first.");
    }
}