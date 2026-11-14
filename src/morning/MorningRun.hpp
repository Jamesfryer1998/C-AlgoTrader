#pragma once

#include <chrono>
#include <ostream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * MorningRun
 *
 * morning_app from start to finish: read config/morning.json, download what's missing, backtest, then save
 * data/morning/report.json and trades.csv and print the verdicts. While it runs, data/morning/progress.json
 * says how far it has got, for the dashboard's progress bar:
 *
 *   {"state": "running", "stage": "Picking and trading each day", "done": 120, "total": 790,
 *    "started": 1794600000, "updated": 1794600360}
 *
 *   morning_app              development and validation only
 *   morning_app --holdout    the holdout too: do this once, at the end (it's recorded)
 *   morning_app --offline    only what's already downloaded
 */
class MorningRun
{
    public:
        struct Options
        {
            bool holdout = false;
            bool offline = false;
            bool showHelp = false;
        };

        static Options parse(const std::vector<std::string>& arguments);   // throws std::invalid_argument
        static void printUsage(std::ostream& out);

        // algoConfig: the merged config; morningConfig: config/morning.json; dataRoot: data/
        MorningRun(const json& algoConfig, const json& morningConfig, const std::string& dataRoot);

        // 0 when the report was saved
        int run(const Options& options);

    private:
        void saveProgress(const std::string& state, const std::string& stage, size_t done, size_t total, bool force);

        json algoConfig;
        json morningConfig;
        std::string folder;
        std::time_t started = 0;
        std::chrono::steady_clock::time_point lastSaved;
};
