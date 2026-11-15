#pragma once

#include <ostream>
#include <string>
#include <nlohmann/json.hpp>
#include "../morning/MorningRun.hpp"

using json = nlohmann::json;

/**
 * GridRun
 *
 * grid_app from start to finish: read config/grid.json, get the daily bars (shared with pairs_app, in
 * data/pairs/), pick the most traded stocks as of the start, trade each variant next to the same sleeves held
 * untouched and with coin-flipped directions, then save data/grid/report.json (the report the Research tab
 * shows) and print the verdicts.
 *
 *   grid_app              development and validation only
 *   grid_app --holdout    the holdout too: once, at the end (recorded in data/grid/holdout_opened.json)
 *   grid_app --offline    only what's already downloaded
 */
class GridRun
{
    public:
        // algoConfig: the merged config; gridConfig: config/grid.json; dataRoot: data/
        GridRun(const json& algoConfig, const json& gridConfig, const std::string& dataRoot);

        // 0 when the report was saved. Options as morning_app's.
        int run(const MorningRun::Options& options);

        static void printUsage(std::ostream& out);

    private:
        json algoConfig;
        json gridConfig;
        std::string dataRoot;
        std::string folder;
};
