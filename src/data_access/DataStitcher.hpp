#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <regex>
#include "MarketCondition.hpp"
#include "../util/DateTimeConversion.hpp"

namespace fs = std::filesystem;

class DataStitcher {
public:
    DataStitcher(const std::string& dataDir, const std::string& ticker);
    
    // Main function to stitch data and get a continuous series
    std::vector<MarketCondition> getStitchedData(const std::string& startDate, const std::string& endDate);
    
    // Function to save stitched data to a new CSV file
    bool saveStitchedData(const std::vector<MarketCondition>& data, const std::string& outputFilePath);
    
    // Helper to find files within a date range
    std::vector<std::string> findFilesInRange(const std::string& startDate, const std::string& endDate);
    
private:
    std::string dataDirectory;
    std::string tickerSymbol;
    
    // Helper functions
    std::string extractDateFromFilename(const std::string& filename);
    std::vector<MarketCondition> readCSVFile(const std::string& filePath);
    std::vector<MarketCondition> mergeDataSeries(const std::vector<std::vector<MarketCondition>>& allData);
    bool isDateInRange(const std::string& dateToCheck, const std::string& startDate, const std::string& endDate);
};