#pragma once

#include <ostream>
#include <string>
#include <nlohmann/json.hpp>
#include "../morning/MorningRun.hpp"

using json = nlohmann::json;

/**
 * RebalanceRun
 *
 * rebalance_app from start to finish: read config/rebalance.json, get the stock and bond funds' daily bars
 * (data/rebalance/daily, dividends and coupons in), trade each variant against coin-flipped versions of itself,
 * then save data/rebalance/report.json (the report the Research tab shows) and print the verdicts and how
 * many coin flips each beat.
 *
 *   rebalance_app              development and validation only
 *   rebalance_app --holdout    the holdout too: once, at the end (recorded in data/rebalance/holdout_opened.json)
 *   rebalance_app --offline    only what's already downloaded
 */
class RebalanceRun
{
    public:
        // algoConfig: the merged config; rebalanceConfig: config/rebalance.json; dataRoot: data/
        RebalanceRun(const json& algoConfig, const json& rebalanceConfig, const std::string& dataRoot);

        // 0 when the report was saved. Options as morning_app's.
        int run(const MorningRun::Options& options);

        static void printUsage(std::ostream& out);

    private:
        json algoConfig;
        json rebalanceConfig;
        std::string dataRoot;
        std::string folder;
};
