#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../morning/MorningSettings.hpp"

using json = nlohmann::json;

/**
 * MomentumSettings
 *
 * momentum_app's rules, from config/momentum.json (see its comments): the dates (the holdout starts when the
 * noise-area paper was published, so it's out of sample for its rules), the symbols, and each variant's rule,
 * sizing and costs.
 */
struct MomentumSettings
{
    enum class Kind { NoiseArea, LastHalfHour, Overnight };

    struct Variant
    {
        std::string name;
        Kind kind = Kind::NoiseArea;
        double bandMultiplier = 1.0;    // noise area: the band is this many times the average move
        int lookbackDays = 14;          // ... averaged over this many days (and the volatility for sizing)
        int checkMinutes = 30;          // ... looked at every this many minutes from the first check
        bool vwapStop = true;           // ... out when back through the band or the day's VWAP
        double targetVolatility = 0.02; // daily; 0: always max_leverage
        double maxLeverage = 4.0;
        double costPerShare = 0.01;     // dollars, each fill
        double slippagePercent = 0.0;   // as well, each fill
    };

    static inline const std::string NOISE_STEP = "Noise-area breakout, as published";
    static inline const std::string HALF_HOUR_STEP = "First half hour, last half hour";
    static inline const std::string OVERNIGHT_STEP = "Holding overnight only";

    std::string start, developmentEnd, validationEnd, end, dataStart;
    std::vector<std::string> symbols;
    std::string benchmark = "SPY";
    double startingCapital = 100000;
    unsigned randomSeed = 7;
    std::vector<Variant> variants;
    json gates = json::object();

    static MomentumSettings fromConfig(const json& config, const std::string& yesterday);

    // A variant's name on one symbol, as the report shows it
    static std::string lineName(const std::string& symbol, const Variant& variant);

    // What the shared research report needs (each symbol x variant is a rule)
    MorningSettings forReport() const;
};
