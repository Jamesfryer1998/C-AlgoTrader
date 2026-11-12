#pragma once

#include <ctime>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "ScreenBars.hpp"
#include "ScreenOptions.hpp"
#include "ScreenSettings.hpp"
#include "../backtest/Screener.hpp"

using json = nlohmann::json;

/**
 * StockScreen
 *
 * One run of screen_app: download the bars, screen every stock (saving results as it goes so --resume can
 * carry on), then print the report and the top picks. Or just download, or just report.
 *
 * Results go in data/screen/minute/ (or data/screen/daily/): screen_results.csv, top_picks.csv and
 * top_picks_config.json.
 */
class StockScreen
{
    public:
        StockScreen(const json& baseConfig, const std::string& projectRoot, const std::string& dataRoot,
                    const ScreenOptions& options);

        // 0 on success, 1 if there was nothing to report
        int run();

    private:
        void downloadAll();
        void saveProgress(const std::string& state, size_t done, const std::string& stock, std::time_t startedAt) const;
        std::vector<Screener::Row> screenAll();
        void printResults(const std::vector<Screener::Row>& rows);
        void printStockLine(size_t index, const std::string& symbol, const std::vector<Screener::Row>& stockRows,
                            double seconds, double minutesLeft) const;

        ScreenOptions options;
        ScreenSettings screen;
        std::string resultsFolder;
        std::string resultsPath;
        Screener screener;
        ScreenBars bars;
};
