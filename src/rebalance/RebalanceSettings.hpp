#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../morning/MorningSettings.hpp"

using json = nlohmann::json;

/**
 * RebalanceSettings
 *
 * rebalance_app's rules, from config/rebalance.json (see its comments): the dates (the holdout starts when the
 * paper came out), the stock and bond funds, the 60/40 portfolio whose drift is the signal, the window it's
 * traded in, costs, and the variants.
 */
struct RebalanceSettings
{
    enum class Legs { Spread, Stocks, StocksLongOnly };   // stocks less bonds; stocks alone; stocks, never short
    enum class Sizing { Sign, Scaled };                   // all in either way; or in proportion to the drift

    struct Variant
    {
        std::string name;
        std::string bond;
        Legs legs = Legs::Spread;
        Sizing sizing = Sizing::Scaled;
        double slippagePercent = 0.01;
    };

    std::string start, developmentEnd, validationEnd, end, dataStart;
    std::string stocks = "SPY";
    std::vector<std::string> bonds;
    std::string adjustment = "all";
    double targetStockWeight = 0.6;
    int windowDays = 5;              // the month's last trading days, when its rebalancing is front-run
    bool reversalDay = true;         // and the next month's first day, the other way
    double fullAtDrift = 0.01;       // Scaled: the drift (stock weight - target) that's a whole position
    double startingCapital = 100000;
    int randomDraws = 200;
    unsigned randomSeed = 7;
    std::vector<Variant> variants;
    json gates = json::object();

    static RebalanceSettings fromConfig(const json& config, const std::string& yesterday);
    MorningSettings forReport() const;
};
