#pragma once

#include <ostream>
#include <string>
#include <nlohmann/json.hpp>
#include "../morning/MorningRun.hpp"

using json = nlohmann::json;

/**
 * IntradayRun
 *
 * intraday_app from start to finish: read config/intraday.json, get the daily bars (shared with pairs_app, in
 * data/pairs/) to pick each month's stocks, download those stocks' and their funds' 1-minute bars day by day
 * (data/intraday/minute/), trade each variant against random stocks, then save data/intraday/report.json (the
 * report the Research tab shows) and print the verdicts.
 *
 *   intraday_app              development and validation only
 *   intraday_app --holdout    the holdout too: once, at the end (recorded in data/intraday/holdout_opened.json)
 *   intraday_app --offline    only what's already downloaded
 */
class IntradayRun
{
    public:
        // algoConfig: the merged config; intradayConfig: config/intraday.json; dataRoot: data/
        IntradayRun(const json& algoConfig, const json& intradayConfig, const std::string& dataRoot);

        // 0 when the report was saved. Options as morning_app's.
        int run(const MorningRun::Options& options);

        static void printUsage(std::ostream& out);

    private:
        json algoConfig;
        json intradayConfig;
        std::string dataRoot;
        std::string folder;
};
