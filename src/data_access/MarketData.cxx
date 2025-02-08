#include "MarketData.hpp"
#include <unistd.h>


MarketData::MarketData() 
{
    
}


MarketData::~MarketData() 
{
}

void
MarketData::process()
{
    std::cout << "Processing Market Data" << std::endl;

    std::cout << "Collecting data every 10 seconds" << std::endl;
    while (true)
    {
        usleep(10 * 1000);
        loadData(FILE_PATH);
    }
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

std::vector<MarketCondition>
MarketData::getData() const
{
    return data;
}