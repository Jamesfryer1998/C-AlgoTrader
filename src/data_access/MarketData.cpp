#include "MarketData.hpp"
#include <stdexcept>

MarketData::MarketData() 
{
}

MarketData::~MarketData() 
{
}

void
MarketData::loadData(const std::string& filePath)
{
    CSVParser parser;
    parser.Read(filePath);
    update(parser.GetData());
}

void
MarketData::update(std::vector<MarketCondition>& marketData)
{
    data = marketData;
}

void
MarketData::addBar(const MarketCondition& bar, size_t maxBars)
{
    data.push_back(bar);

    // Drop the oldest bars once we have twice as many as needed, back down to maxBars.
    // Trimming in batches keeps this O(1) per bar on average (removing one bar from the front of a
    // vector every time would shift the whole window on every bar). Strategies only ever look at
    // the newest bars, so how many older ones are still held doesn't change any result.
    if (maxBars > 0 && data.size() > 2 * maxBars)
        data.erase(data.begin(), data.begin() + (data.size() - maxBars));
}

const vector<MarketCondition>&
MarketData::getData() const
{
    return data;
}

float
MarketData::getLastClosePrice() const
{
    if (data.empty()) {
        throw std::runtime_error("No market data available");
    }
    
    // Return the most recent close price
    return data.back().Close;
}

MarketCondition
MarketData::getCurrentData() const
{
    if (data.empty()) {
        throw std::runtime_error("No market data available");
    }
    
    // Return the most recent data point
    return data.back();
}