#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "AlpacaClient.hpp"
#include "../data_access/MarketCondition.hpp"

using json = nlohmann::json;

/**
 * AlpacaHistory
 *
 * Downloads historical 1-minute bars from Alpaca and saves them as CSV files the backtester reads.
 *
 *   data/marketData_NVDA_2026-10-28.csv   one file per trading day, New York exchange time
 *   Datetime,Ticker,Open,Close,Volume,TimeInterval
 *   2026-10-28 09:30:00,NVDA,181.90,181.72,7189234,1m
 *
 * Config (src/config/config.json -> "alpaca"):
 *   "history_feed": "sip" -> all US exchanges. Free on the Basic plan for anything older than 15 minutes.
 *                   "iex" -> IEX only (matches the free live stream, much lower volumes)
 */
class AlpacaHistory
{
    public:
        AlpacaHistory(const json& config);

        // Download every trading day from startDate to endDate (YYYY-MM-DD, inclusive) into dataDir.
        // Days that already have a file are skipped unless overwrite is true.
        // Returns the number of bars written, or -1 if Alpaca could not be reached.
        int downloadToCsv(const std::string& startDate, const std::string& endDate,
                          const std::string& dataDir, bool overwrite = false);

        // All 1-minute bars between two UTC times (RFC3339), oldest first, times converted to New York.
        // Handles Alpaca's paging. Sets ok=false if a request failed.
        std::vector<MarketCondition> fetchBars(const std::string& startUtc, const std::string& endUtc, bool& ok);

        // Path of the CSV for one day, e.g. <dataDir>/marketData_NVDA_2026-10-28.csv
        std::string csvPath(const std::string& dataDir, const std::string& date) const;

        // Write bars to a CSV in the backtester's format
        static bool writeCsv(const std::string& path, const std::vector<MarketCondition>& bars);

    private:
        std::string symbol;
        std::string feed;
        std::string dataUrl;
        std::string fileBaseName;
        bool regularHoursOnly;
        AlpacaClient client;
};
