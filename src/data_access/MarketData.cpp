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
MarketData::process()
{
    std::cout << "Processing Market Data" << std::endl;

    // Get Config data
    Config config;
    json configData = config.getJson();
    int runInterval = configData["run_interval"];
    string filePath = generateFilePath(configData);

    std::cout << "Collecting data every " << runInterval << " seconds" << std::endl;
    loadData(filePath);
    std::this_thread::sleep_for(std::chrono::seconds(runInterval));
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
    return exePath.string();  // Returns the directory where the executable is run
}