#include "DataStitcher.hpp"

namespace
{
    // The date in a file name, e.g. marketData_NVDA_2025-03-31.csv
    const std::regex DATE_IN_FILE_NAME(".*_(\\d{4}-\\d{2}-\\d{2})\\.csv");
}

DataStitcher::DataStitcher(const std::string& dataDir, const std::string& ticker)
    : dataDirectory(dataDir), tickerSymbol(ticker) {
}

std::vector<std::string> 
DataStitcher::findFilesInRange(const std::string& startDate, const std::string& endDate) {
    std::vector<std::string> matchingFiles;
    
    for (const auto& entry : fs::directory_iterator(dataDirectory)) {
        if (entry.is_regular_file() && entry.path().extension() == CSV_EXTENSION) {
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

std::string 
DataStitcher::extractDateFromFilename(const std::string& filename) {
    std::smatch matches;
    
    if (std::regex_search(filename, matches, DATE_IN_FILE_NAME) && matches.size() > 1) {
        return matches[1].str();
    }
    
    return ""; // Return empty string if date extraction fails
}