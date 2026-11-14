#pragma once

#include <ostream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../morning/MorningRun.hpp"

using json = nlohmann::json;

/**
 * PairsRun
 *
 * pairs_app from start to finish: read config/pairs.json, download the daily bars it needs into data/pairs/,
 * backtest pairs trading and lead-lag against random pairs and leaders, then save data/pairs/report.json (the
 * same report the morning screen writes, so the dashboard's Research tab shows both) and print the verdicts.
 *
 *   pairs_app              development and validation only
 *   pairs_app --holdout    the holdout too: once, at the end (recorded in data/pairs/holdout_opened.json)
 *   pairs_app --offline    only what's already downloaded
 */
class PairsRun
{
    public:
        // algoConfig: the merged config; pairsConfig: config/pairs.json; dataRoot: data/
        PairsRun(const json& algoConfig, const json& pairsConfig, const std::string& dataRoot);

        // 0 when the report was saved. Options as morning_app's.
        int run(const MorningRun::Options& options);

        static void printUsage(std::ostream& out);

    private:
        // The symbols worth downloading: those averaging at least prefilter_dollar_volume a day in the morning
        // screen's daily bars, if it has some; otherwise all of them
        std::vector<std::string> worthDownloading(const std::vector<std::string>& symbols, double prefilter) const;

        json algoConfig;
        json pairsConfig;
        std::string dataRoot;
        std::string folder;
};
