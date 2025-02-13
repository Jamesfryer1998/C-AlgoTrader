#include <unistd.h>

#include "MarketData.hpp"
#include "../util/Config.hpp"


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

    // Get Config data
    Config config;
    json configData = config.getJson();
    int runInterval = configData["run_interval"];
    std::string filePath = configData["marketDataBasePath"];

    // Add the datetime to the end of the file name path here

    std::cout << "Collecting data every " << runInterval << " seconds" << std::endl;
    while (true)
    {
        usleep(runInterval * 1000);
        loadData(filePath);
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