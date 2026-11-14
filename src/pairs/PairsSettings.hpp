#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../morning/MorningSettings.hpp"

using json = nlohmann::json;

/**
 * PairsSettings
 *
 * pairs_app's rules, from config/pairs.json (see its comments): the dates, which stocks each month, how pairs
 * are formed and traded, how lead-lag picks its leaders, money and costs, and the variants tried.
 */
struct PairsSettings
{
    enum class Kind { Pairs, LeadLag };

    struct Variant
    {
        std::string name;
        Kind kind = Kind::Pairs;
        int delayDays = 1;       // pairs: trade this many days after the signal
        int holdDays = 1;        // lead-lag: days held
    };

    static inline const std::string PAIRS_STEP = "Pairs: trading the gap";
    static inline const std::string LEAD_LAG_STEP = "Lead-lag: following the leader";

    std::string start, developmentEnd, validationEnd, end, dataStart;

    // Universe
    size_t universeSize = 500;
    double minPrice = 5.0;
    double minDollarVolume = 10e6;
    double prefilterDollarVolume = 5e6;

    int formationDays = 252;

    // Pairs
    size_t pairs = 20;
    int tradingDays = 126;
    double openSigma = 2.0;

    // Lead-lag
    size_t stocksEachSide = 20;
    double minCorrelation = 0.5;

    double startingCapital = 100000;
    double slippagePercent = 0.03;
    double leverage = 1.0;
    unsigned randomSeed = 7;

    std::vector<Variant> variants;
    json gates;

    // From config/pairs.json; `end` empty -> `yesterday`
    static PairsSettings fromConfig(const json& config, const std::string& yesterday);

    // What the shared research report needs: the dates, costs, pass marks and each variant's step
    MorningSettings forReport() const;
};
