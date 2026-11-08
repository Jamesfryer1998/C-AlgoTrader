#pragma once

#include <filesystem>
#include <regex>
#include <string>
#include <vector>
#include "MarketCondition.hpp"

namespace fs = std::filesystem;

class DataStitcher {
public:
    DataStitcher(const std::string& dataDir, const std::string& ticker);

    // The ticker's files in dataDir whose names are dated from startDate to endDate (YYYY-MM-DD), e.g.
    // marketData_NVDA_2025-03-31.csv
    std::vector<std::string> findFilesInRange(const std::string& startDate, const std::string& endDate);

private:
    std::string dataDirectory;
    std::string tickerSymbol;

    std::string extractDateFromFilename(const std::string& filename);
};