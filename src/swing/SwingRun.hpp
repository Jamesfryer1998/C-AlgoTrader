#pragma once

#include <ostream>
#include <string>
#include <nlohmann/json.hpp>
#include "../morning/MorningRun.hpp"

using json = nlohmann::json;

/**
 * SwingRun
 *
 * swing_app from start to finish: read config/swing.json, get the daily bars (shared with pairs_app, in
 * data/pairs/), backtest each swing signal against random buys, then save data/swing/report.json (the report
 * the Research tab shows) and print the verdicts.
 *
 *   swing_app              development and validation only
 *   swing_app --holdout    the holdout too: once, at the end (recorded in data/swing/holdout_opened.json)
 *   swing_app --offline    only what's already downloaded
 */
class SwingRun
{
    public:
        // algoConfig: the merged config; swingConfig: config/swing.json; dataRoot: data/
        SwingRun(const json& algoConfig, const json& swingConfig, const std::string& dataRoot);

        // 0 when the report was saved. Options as morning_app's.
        int run(const MorningRun::Options& options);

        static void printUsage(std::ostream& out);

    private:
        json algoConfig;
        json swingConfig;
        std::string dataRoot;
        std::string folder;
};
