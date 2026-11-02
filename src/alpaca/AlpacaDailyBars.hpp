#pragma once

#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "AlpacaClient.hpp"
#include "../quant/DailyBars.hpp"

using json = nlohmann::json;

/**
 * AlpacaDailyBars
 *
 * Downloads daily bars for the quant projects and caches them as data/daily/<SYMBOL>.csv.
 * Prices are adjusted for splits and dividends ("adjustment=all"). Uses the SIP feed (all US exchanges),
 * free on Alpaca's Basic plan for anything older than 15 minutes; Alpaca's history starts in 2016.
 * Today's unfinished bar is left out.
 *
 * Keys come from .env (APCA_API_KEY_ID / APCA_API_SECRET_KEY), as for the live app.
 */
class AlpacaDailyBars
{
    public:
        AlpacaDailyBars(const json& config, const std::string& dataDir);

        // Daily bars from startDate to endDate (YYYY-MM-DD; empty endDate = up to yesterday).
        // Downloads and refreshes the cached CSV unless offline is true; if the download fails the cached
        // file is used instead (with a warning). Empty if neither works.
        std::vector<quant::DailyBar> load(const std::string& symbol, const std::string& startDate,
                                          const std::string& endDate, bool offline);

        // Load several symbols; stops with an error message (returns false) if any is missing
        bool loadAll(const std::vector<std::string>& symbols, const std::string& startDate,
                     const std::string& endDate, bool offline,
                     std::map<std::string, std::vector<quant::DailyBar>>& out);

        // Convert one Alpaca bars response into daily bars (public for tests). Skips `skipDate` (today).
        static std::vector<quant::DailyBar> parseBars(const json& body, const std::string& skipDate = "");

        std::string csvPath(const std::string& symbol) const;

    private:
        bool download(const std::string& symbol, const std::string& startDate, const std::string& endDate,
                      std::vector<quant::DailyBar>& bars);

        AlpacaClient client;
        std::string dataUrl;
        std::string feed;
        std::string dataDir;
};
