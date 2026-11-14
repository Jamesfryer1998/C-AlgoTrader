#pragma once

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
 * says how far it has got (ResearchProgress).
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
        json algoConfig;
        json morningConfig;
        std::string folder;
};
