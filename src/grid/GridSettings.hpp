#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../morning/MorningSettings.hpp"

using json = nlohmann::json;

/**
 * GridSettings
 *
 * grid_app's rules, from config/grid.json (see its comments): the dates, which stocks and funds are traded,
 * how much of each sleeve starts invested, and each variant's trigger (the day's move), step (how much of the
 * position is bought or sold), direction and fill.
 */
struct GridSettings
{
    enum class Universe { Stocks, Funds };

    struct Variant
    {
        std::string name;
        Universe universe = Universe::Stocks;
        double triggerPercent = 5.0;   // a close this far down (or up) on the close before
        double stepPercent = 5.0;      // buys (or sells) this much of what's held
        bool mirror = false;           // the other way: buy after a rise, sell after a fall
        bool atClose = false;          // filled at the signal's close instead of the next open
    };

    std::string start, developmentEnd, validationEnd, end, dataStart;
    size_t stocks = 200;               // the most traded, as of the start
    int formationDays = 60;
    double minPrice = 5.0;
    double prefilterDollarVolume = 5e6;
    double loadDollarVolume = 50e6;
    std::vector<std::string> funds;
    std::vector<std::string> notStocks;
    double startInvested = 0.8;        // of each sleeve, the rest cash to buy drops with
    double slippagePercent = 0.03;
    double startingCapital = 100000;
    unsigned randomSeed = 7;
    std::vector<Variant> variants;
    json gates = json::object();

    static GridSettings fromConfig(const json& config, const std::string& yesterday);
    MorningSettings forReport() const;
};
