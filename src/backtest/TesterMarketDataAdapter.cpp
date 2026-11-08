#include "TesterMarketDataAdapter.hpp"
#include "../util/Log.hpp"
#include <stdexcept>
#include <iostream>

TesterMarketDataAdapter::TesterMarketDataAdapter()
    : marketData(nullptr), currentIndex(0)
{
}

TesterMarketDataAdapter::~TesterMarketDataAdapter()
{
    // We don't own the marketData pointer, so don't delete it
}

void 
TesterMarketDataAdapter::initialize(MarketData& marketDataInstance)
{
    marketData = &marketDataInstance;
    currentIndex = 0;
}

void 
TesterMarketDataAdapter::loadMockData(const std::vector<MarketCondition>& mockData)
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
    
    std::cout << "TesterMarketDataAdapter: Loaded " << fullDataset.size() 
              << " mock data points" << std::endl;
}

bool 
TesterMarketDataAdapter::hasNext() const
{
    return static_cast<size_t>(currentIndex) < fullDataset.size();
}

void 
TesterMarketDataAdapter::next()
{
    if (!hasNext()) {
        return;
    }
    
    // Get the current market condition at the current index
    MarketCondition currentCondition = fullDataset[currentIndex];
    
    // MarketData holds every bar up to the current index so strategies can see history.
    // Append just the new bar rather than rebuilding the whole list each step (that made long backtests O(n^2)).
    if (currentIndex == 0) {
        std::vector<MarketCondition> firstBar = { currentCondition };
        marketData->update(firstBar);
    } else {
        marketData->addBar(currentCondition);
    }
    
    // Advance the index
    Log::out() << "DEBUG: Processing timepoint [" << currentCondition.DateTime 
              << "] at index " << currentIndex << " (data size: " << marketData->getData().size() << ")" << std::endl;
    currentIndex++;
}

void 
TesterMarketDataAdapter::rewind()
{
    Log::out() << "Rewinding to the beginning of the data" << std::endl;
    currentIndex = 0;
    
    // If we have data, update the MarketData instance with the first point
    if (!fullDataset.empty()) {
        // Update with the first data point only
        std::vector<MarketCondition> initialData = { fullDataset[0] };
        marketData->update(initialData);
    }
}

int 
TesterMarketDataAdapter::getCurrentIndex() const
{
    return currentIndex;
}

size_t 
TesterMarketDataAdapter::getDataSize() const
{
    return fullDataset.size();
}

MarketCondition 
TesterMarketDataAdapter::getCurrentData() const
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

void 
TesterMarketDataAdapter::validateMarketData() const
{
    if (marketData == nullptr) {
        throw std::runtime_error("MarketData instance not initialized. Call initialize() first.");
    }
}