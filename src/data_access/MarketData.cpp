#include <unistd.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include "MarketData.hpp"
#include "../util/Config.hpp"
#include "../util/DateTimeConversion.hpp"


MarketData::MarketData() 
{
}

MarketData::~MarketData() 
{
}

void
MarketData::process(json configData)
{
    std::cout << "Processing Market Data" << std::endl;
    string filePath = generateFilePath(configData);
    loadData(filePath);
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

vector<MarketCondition>
MarketData::getData() const
{
    return data;
}

string
MarketData::generateFilePath(json configData)
{
    string projectRoot = getProjectRoot() + "/";
    string fileBasePath = configData["marketDataBasePath"];
    string fileBaseName = configData["baseDataFileName"];
    string ticker = configData["ticker"];
    string date = DateTimeConversion().timeNowToDate();
    string filePath = projectRoot + fileBasePath + fileBaseName + "_" + ticker + "_" + date + ".csv";
    return filePath;
}

string 
MarketData::getProjectRoot()
{
    std::filesystem::path exePath = std::filesystem::current_path();
    return exePath.string();
}

float
MarketData::getLastClosePrice()
{
    return data.back().Close;
}

void 
MarketData::rewind() 
{
    std::cout << "Rewinding to the beginning of the data" << std::endl;
    currentIndex = 0;
}

bool 
MarketData::hasNext() 
{
    std::cout << "Checking if we have more data..." << std::endl;
    std::cout << "we have " << data.size()  << std::endl;
    return static_cast<size_t>(currentIndex) < data.size();
}

void 
MarketData::next() 
{
    std::cout << "Processing next data..." << std::endl;

    if (hasNext()) {
        currentData = data[currentIndex];
        currentIndex++;
        std::cout << "On timestamp: " << currentData.DateTime <<  std::endl;
    }
}