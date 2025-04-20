#include <unistd.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <future>
#include <iomanip>
#include <sstream>
#include "MarketData.hpp"
#include "../util/Config.hpp"
#include "../util/DateTimeConversion.hpp"

MarketData::MarketData() 
: currentIndex(0)
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
MarketData::processForBacktest(json configData, const std::string& startDate, const std::string& endDate)
{
    std::cout << "Processing Market Data for backtesting from " << startDate << " to " << endDate << std::endl;
    
    // Get ticker from config
    std::string ticker = configData["ticker"];
    
    // Get data directory
    std::string dataDir = getDataDirectory();
    
    // Create a data stitcher
    DataStitcher stitcher(dataDir, ticker);
    
    // Get stitched data
    std::vector<MarketCondition> stitchedData = stitcher.getStitchedData(startDate, endDate);
    
    // Update our data with the stitched data
    update(stitchedData);
    
    // If no data was found, try loading a single file as fallback
    if (data.empty()) {
        std::cout << "No stitched data found, falling back to single file" << std::endl;
        string filePath = generateFilePath(configData);
        loadData(filePath);
    }
    
    // Sort data by datetime
    std::sort(data.begin(), data.end(), 
              [](const MarketCondition& a, const MarketCondition& b) {
                  return a.DateTime < b.DateTime;
              });
    
    std::cout << "Loaded " << data.size() << " market data points for backtesting" << std::endl;
}

void
MarketData::processParallel(json configData, int numThreads)
{
    // If numThreads is 0, use the number of hardware threads available
    if (numThreads <= 0) {
        numThreads = std::thread::hardware_concurrency();
    }
    
    std::cout << "Processing Market Data in parallel using " << numThreads << " threads" << std::endl;
    
    // Get ticker and date range from config
    std::string ticker = configData["ticker"];
    
    // Default to last 7 days if not specified
    std::string endDate = DateTimeConversion().timeNowToDate();
    
    // Calculate start date (7 days before end date)
    auto now = std::chrono::system_clock::now();
    auto startTime = now - std::chrono::hours(24 * 7);
    auto startTimeT = std::chrono::system_clock::to_time_t(startTime);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&startTimeT), "%Y-%m-%d");
    std::string startDate = ss.str();
    
    // Check if date range is specified in config
    if (configData.contains("backtest_start_date") && configData.contains("backtest_end_date")) {
        startDate = configData["backtest_start_date"];
        endDate = configData["backtest_end_date"];
    }
    
    // Get data directory
    std::string dataDir = getDataDirectory();
    
    // Create a data stitcher
    DataStitcher stitcher(dataDir, ticker);
    
    // Find all files in the date range
    std::vector<std::string> files = stitcher.findFilesInRange(startDate, endDate);
    
    if (files.empty()) {
        std::cerr << "No data files found for ticker " << ticker 
                  << " between " << startDate << " and " << endDate << std::endl;
        return;
    }
    
    // Distribute files across threads
    std::vector<std::vector<std::string>> fileGroups(numThreads);
    for (size_t i = 0; i < files.size(); ++i) {
        fileGroups[i % numThreads].push_back(files[i]);
    }
    
    // Create thread pool
    std::vector<std::future<std::vector<MarketCondition>>> futures;
    
    // Launch threads
    for (int i = 0; i < numThreads; ++i) {
        futures.push_back(std::async(std::launch::async, [this, fileGroups, i]() {
            std::vector<MarketCondition> threadData;
            
            for (const auto& file : fileGroups[i]) {
                CSVParser parser;
                parser.Read(file);
                auto fileData = parser.GetData();
                
                // Append this file's data to thread's data
                threadData.insert(threadData.end(), fileData.begin(), fileData.end());
            }
            
            return threadData;
        }));
    }
    
    // Collect results
    std::vector<MarketCondition> allData;
    for (auto& future : futures) {
        auto threadData = future.get();
        allData.insert(allData.end(), threadData.begin(), threadData.end());
    }
    
    // Remove duplicates by creating a map keyed by datetime
    std::map<std::string, MarketCondition> uniqueData;
    for (const auto& condition : allData) {
        uniqueData[condition.DateTime] = condition;
    }
    
    // Convert back to vector
    allData.clear();
    for (const auto& pair : uniqueData) {
        allData.push_back(pair.second);
    }
    
    // Sort by datetime
    std::sort(allData.begin(), allData.end(), 
              [](const MarketCondition& a, const MarketCondition& b) {
                  return a.DateTime < b.DateTime;
              });
    
    // Update our data
    update(allData);
    
    std::cout << "Loaded " << data.size() << " market data points in parallel" << std::endl;
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

string
MarketData::getDataDirectory()
{
    return getProjectRoot() + "/data";
}

float
MarketData::getLastClosePrice()
{
    if (data.empty()) {
        throw std::runtime_error("No market data available");
    }
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
    return static_cast<size_t>(currentIndex) < data.size();
}

void 
MarketData::next() 
{
    if (hasNext()) {
        currentData = data[currentIndex];
        currentIndex++;
    }
}

MarketCondition
MarketData::getCurrentData()
{
    // Check if we have set current data
    // else get from top of vector
    if(currentData.Close == 0 ||
       currentData.Open == 0 ||
       currentData.DateTime == "" ||
       currentData.Ticker == "")

    {
        return data.back();
    }
    // else return (
    
    return currentData;
}

void 
MarketData::setIndexedData()
{
    // Make a backup of the full data

    if (data.empty() || currentIndex <= 0) {
        std::cerr << "Warning: Cannot set indexed data - data is empty or index is invalid" << currentIndex << std::endl;
        return;
    }
 
    // Create a new vector with data up to current index
    std::vector<MarketCondition> limitedData(data.begin(), data.begin() + currentIndex);

    // Replace data with the limited version
    data = limitedData;

    std::cout << "MarketData limited to " << currentIndex << " data points for backtesting" << std::endl;
}