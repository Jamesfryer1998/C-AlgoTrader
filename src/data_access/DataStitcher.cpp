#include "DataStitcher.hpp"
#include <sstream>
#include <map>
#include <chrono>
#include <iomanip>

DataStitcher::DataStitcher(const std::string& dataDir, const std::string& ticker)
    : dataDirectory(dataDir), tickerSymbol(ticker) {
}

std::vector<MarketCondition> 
DataStitcher::getStitchedData(const std::string& startDate, const std::string& endDate) {
    // Find all relevant files in the date range
    std::vector<std::string> files = findFilesInRange(startDate, endDate);
    
    if (files.empty()) {
        std::cerr << "No data files found for ticker " << tickerSymbol 
                  << " between " << startDate << " and " << endDate << std::endl;
        return {};
    }
    
    // Read all data from files
    std::vector<std::vector<MarketCondition>> allDataSeries;
    for (const auto& file : files) {
        std::cout << "Reading data from: " << file << std::endl;
        std::vector<MarketCondition> dataSeries = readCSVFile(file);
        if (!dataSeries.empty()) {
            allDataSeries.push_back(dataSeries);
        }
    }
    
    // Merge all data series
    std::vector<MarketCondition> stitchedData = mergeDataSeries(allDataSeries);
    
    // Filter to only include data in the requested date range
    std::vector<MarketCondition> filteredData;
    for (const auto& condition : stitchedData) {
        // Extract just the date part for comparison
        std::string dateStr = condition.DateTime.substr(0, 10);
        if (isDateInRange(dateStr, startDate, endDate)) {
            filteredData.push_back(condition);
        }
    }
    
    std::cout << "Stitched " << allDataSeries.size() << " data files into " 
              << filteredData.size() << " data points" << std::endl;
    
    return filteredData;
}

std::vector<std::string> 
DataStitcher::findFilesInRange(const std::string& startDate, const std::string& endDate) {
    std::vector<std::string> matchingFiles;
    
    for (const auto& entry : fs::directory_iterator(dataDirectory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".csv") {
            std::string filename = entry.path().filename().string();
            
            // Check if this file matches our ticker
            if (filename.find(tickerSymbol) != std::string::npos) {
                // Extract date from filename
                std::string fileDate = extractDateFromFilename(filename);
                
                // Check if the file's date is within our range
                if (fileDate >= startDate && fileDate <= endDate) {
                    matchingFiles.push_back(entry.path().string());
                }
            }
        }
    }
    
    return matchingFiles;
}

std::vector<MarketCondition> 
DataStitcher::readCSVFile(const std::string& filePath) {
    std::vector<MarketCondition> data;
    std::ifstream file(filePath);
    
    if (!file.is_open()) {
        std::cerr << "Error opening file: " << filePath << std::endl;
        return data;
    }
    
    std::string line;
    bool firstLine = true;
    
    while (std::getline(file, line)) {
        if (firstLine) {
            // Skip header line
            firstLine = false;
            continue;
        }
        
        std::stringstream ss(line);
        std::string dateTime, ticker, open, close, volume, timeInterval;
        
        std::getline(ss, dateTime, ',');
        std::getline(ss, ticker, ',');
        std::getline(ss, open, ',');
        std::getline(ss, close, ',');
        std::getline(ss, volume, ',');
        std::getline(ss, timeInterval, ',');
        
        try {
            MarketCondition condition(
                dateTime,
                ticker,
                std::stof(open),
                std::stof(close),
                std::stoi(volume),
                timeInterval
            );
            
            data.push_back(condition);
        } catch (const std::exception& e) {
            std::cerr << "Error parsing line: " << line << std::endl;
            std::cerr << "Exception: " << e.what() << std::endl;
        }
    }
    
    file.close();
    return data;
}

std::vector<MarketCondition> 
DataStitcher::mergeDataSeries(const std::vector<std::vector<MarketCondition>>& allData) {
    // Use a map to avoid duplicates, with datetime as the key
    std::map<std::string, MarketCondition> mergedDataMap;
    
    for (const auto& dataSeries : allData) {
        for (const auto& condition : dataSeries) {
            mergedDataMap[condition.DateTime] = condition;
        }
    }
    
    // Convert map to vector and sort by datetime
    std::vector<MarketCondition> mergedData;
    for (const auto& pair : mergedDataMap) {
        mergedData.push_back(pair.second);
    }
    
    // Sort by datetime
    std::sort(mergedData.begin(), mergedData.end(), 
              [](const MarketCondition& a, const MarketCondition& b) {
                  return a.DateTime < b.DateTime;
              });
    
    return mergedData;
}

bool 
DataStitcher::saveStitchedData(const std::vector<MarketCondition>& data, const std::string& outputFilePath) {
    std::ofstream outFile(outputFilePath);
    
    if (!outFile.is_open()) {
        std::cerr << "Error opening output file: " << outputFilePath << std::endl;
        return false;
    }
    
    // Write header
    outFile << "Datetime,Ticker,Open,Close,Volume,TimeInterval" << std::endl;
    
    // Write data
    for (const auto& condition : data) {
        outFile << condition.DateTime << ","
                << condition.Ticker << ","
                << condition.Open << ","
                << condition.Close << ","
                << condition.Volume << ","
                << condition.TimeInterval << std::endl;
    }
    
    outFile.close();
    std::cout << "Saved stitched data to: " << outputFilePath << std::endl;
    
    return true;
}

bool 
DataStitcher::isDateInRange(const std::string& dateToCheck, const std::string& startDate, const std::string& endDate) {
    return (dateToCheck >= startDate && dateToCheck <= endDate);
}

std::string 
DataStitcher::extractDateFromFilename(const std::string& filename) {
    // Example filename: marketData_NVDA_2025-03-31.csv
    std::regex pattern(".*_(\\d{4}-\\d{2}-\\d{2})\\.csv");
    std::smatch matches;
    
    if (std::regex_search(filename, matches, pattern) && matches.size() > 1) {
        return matches[1].str();
    }
    
    return ""; // Return empty string if date extraction fails
}