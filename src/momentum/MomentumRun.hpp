#pragma once

#include <ostream>
#include <string>
#include <nlohmann/json.hpp>
#include "../morning/MorningRun.hpp"

using json = nlohmann::json;

/**
 * MomentumRun
 *
 * momentum_app from start to finish: read config/momentum.json, download the symbols' 1-minute bars day by
 * day (data/momentum/minute) on the benchmark's trading days, trade each variant on each symbol against the
 * same trades in random directions, then save data/momentum/report.json (the report the Research tab shows)
 * and print the verdicts. The holdout starts when the noise-area rules were published.
 *
 *   momentum_app              development and validation only
 *   momentum_app --holdout    the holdout too: once, at the end (recorded in data/momentum/holdout_opened.json)
 *   momentum_app --offline    only what's already downloaded
 */
class MomentumRun
{
    public:
        // algoConfig: the merged config; momentumConfig: config/momentum.json; dataRoot: data/
        MomentumRun(const json& algoConfig, const json& momentumConfig, const std::string& dataRoot);

        // 0 when the report was saved. Options as morning_app's.
        int run(const MorningRun::Options& options);

        static void printUsage(std::ostream& out);

    private:
        json algoConfig;
        json momentumConfig;
        std::string dataRoot;
        std::string folder;
};
