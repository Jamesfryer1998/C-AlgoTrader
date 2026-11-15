#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "../morning/MorningSettings.hpp"

using json = nlohmann::json;

/**
 * TimingSettings
 *
 * timing_app's settings, from config/timing.json (see its comments): the dates, what's timed and where idle
 * money waits, costs, which rules may be finalists and how many, and the rule families with the values each
 * parameter is tried at (every combination is a rule).
 */
struct TimingSettings
{
    std::string start, developmentEnd, validationEnd, end, dataStart;

    std::string symbol = "SPY";
    std::string cashSymbol = "BIL";   // idle money is held in this (empty: earns nothing)
    std::string adjustment = "all";   // Alpaca's: dividends in, as holding SPY really pays

    double startingCapital = 100000;
    double slippagePercent = 0.03;    // on every change of exposure, both what's sold and what's bought
    double rebalanceBand = 0.05;      // a fractional exposure is only changed when it's this far from its target

    // Who can be a finalist: picked on development only
    double minExposure = 0.2;
    double maxExposure = 0.9;
    int minTrades = 10;               // changes of exposure in development
    size_t finalists = 20;
    size_t perFamily = 4;             // no more finalists from one family, so twenty near-copies can't fill it
    int randomDraws = 200;            // random timings of each finalist, for its percentile
    unsigned randomSeed = 7;
    size_t leaderboardSize = 100;

    json families = json::object();
    json gates = json::object();

    static TimingSettings fromConfig(const json& config, const std::string& yesterday);

    // What the shared research report needs: dates, costs, pass marks (each finalist's step is its family)
    MorningSettings forReport() const;
};
