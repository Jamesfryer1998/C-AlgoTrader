#pragma once

#include <ostream>
#include <string>
#include <nlohmann/json.hpp>
#include "../morning/MorningRun.hpp"

using json = nlohmann::json;

/**
 * TimingStocksRun
 *
 * timing_stocks_app: the timing search (TimingRule's families, every combination) on hundreds of stocks and
 * funds instead of SPY alone. The symbols are the most traded over the shared daily bars (data/pairs), plus a
 * fixed list of funds, each with bars from data_start (data/timing_stocks/daily, dividends in). Two ways of
 * picking, both on the development years only:
 *
 *   each stock's best rule   by its edge (CAGR less the same exposure held without timing), then traded through
 *                            validation against random timings of itself; next to a rule picked at random for
 *                            each stock, which is what picking the best would beat if development predicted
 *                            nothing
 *   rules that travel        each rule's median edge across all the stocks; the best few (a few a family) are
 *                            traded on every stock as an equal-weighted portfolio, next to the same with random
 *                            timings and every stock simply held
 *
 * Saves data/timing_stocks/report.json (the finalists' portfolios, as every study, plus the leaderboard and
 * each stock's pick).
 */
class TimingStocksRun
{
    public:
        // algoConfig: the merged config; timingConfig: config/timing_stocks.json; dataRoot: data/
        TimingStocksRun(const json& algoConfig, const json& timingConfig, const std::string& dataRoot);

        // 0 when the report was saved. Options as morning_app's.
        int run(const MorningRun::Options& options);

        static void printUsage(std::ostream& out);

    private:
        json algoConfig;
        json timingConfig;
        std::string dataRoot;
        std::string folder;
};
