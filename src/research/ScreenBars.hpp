#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "ScreenSettings.hpp"
#include "../alpaca/AlpacaDailyBars.hpp"
#include "../data_access/MarketCondition.hpp"

using json = nlohmann::json;

/**
 * ScreenBars
 *
 * Where a screen's bars come from: daily bars in data/daily/<SYMBOL>.csv, minute bars in data/minute/<SYMBOL>/
 * (one CSV per day). Downloads from Alpaca, or loads what's saved.
 */
class ScreenBars
{
    public:
        ScreenBars(const json& baseConfig, const std::string& dataRoot, const ScreenSettings& screen, int threads);

        // Download (or top up) one stock's bars. False if the download failed.
        bool download(const std::string& symbol);

        // The bars saved for one stock, oldest first (empty if none)
        std::vector<MarketCondition> load(const std::string& symbol);

    private:
        std::string minuteFolder(const std::string& symbol) const;

        json baseConfig;
        std::string dataRoot;
        const ScreenSettings& screen;
        int threads;
        AlpacaDailyBars dailySource;
};
