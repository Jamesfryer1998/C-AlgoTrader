#pragma once

#include <ostream>
#include <string>
#include <nlohmann/json.hpp>
#include "../morning/MorningRun.hpp"

using json = nlohmann::json;

/**
 * TimingRun
 *
 * timing_app from start to finish: read config/timing.json, get the timed symbol's and the cash fund's daily
 * bars (data/timing/daily, dividends in), trade every rule, rank them on the development years alone by what
 * their timing added over holding the same exposure without it, take the best few of each family as
 * finalists, trade those through validation (and the holdout) against random timings, then save
 * data/timing/report.json (the finalists, as the Research tab shows every study, plus the leaderboard) and
 * data/timing/rules.csv (every rule's development numbers).
 *
 *   timing_app              development and validation only
 *   timing_app --holdout    the holdout too: once, at the end (recorded in data/timing/holdout_opened.json)
 *   timing_app --offline    only what's already downloaded
 */
class TimingRun
{
    public:
        // algoConfig: the merged config; timingConfig: config/timing.json; dataRoot: data/
        TimingRun(const json& algoConfig, const json& timingConfig, const std::string& dataRoot);

        // 0 when the report was saved. Options as morning_app's.
        int run(const MorningRun::Options& options);

        static void printUsage(std::ostream& out);

    private:
        json algoConfig;
        json timingConfig;
        std::string dataRoot;
        std::string folder;
};
