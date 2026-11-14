#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * MorningSettings
 *
 * The morning screen's rules, from config/morning.json (see its comments): the dates and their split into
 * development / validation / holdout, which stocks can be picked, how the opening range breakout trades, the
 * costs, the variants tried and the pass marks. Fixed before a run: nothing here is tuned by the run.
 */
struct MorningSettings
{
    enum class EntryFill { NextOpen, Touch };

    // The three stretches of the data: rules are developed on the first, checked on the second, and the
    // third is looked at once, at the end
    static inline const std::string DEVELOPMENT = "development";
    static inline const std::string VALIDATION = "validation";
    static inline const std::string HOLDOUT = "holdout";

    struct Variant
    {
        std::string name;
        EntryFill entryFill = EntryFill::NextOpen;
        int holdMinutes = 0;      // 0: hold to the end of the day
        double targetR = 0;       // 0: no profit target
        json strategy;            // a live strategy's settings (e.g. SCALP) instead of the breakout

        bool isStrategy() const { return strategy.is_object() && !strategy.empty(); }
    };

    // Dates (YYYY-MM-DD)
    std::string start, developmentEnd, validationEnd, end;

    // Universe, from the lookbackDays before each day
    double minPrice = 5.0;
    double minAverageVolume = 1e6;
    double minAtr = 0.5;
    int lookbackDays = 14;
    int minHistoryDays = 10;

    // Selection
    int stocks = 20;
    double minRelativeVolume = 1.0;
    int openingMinutes = 5;
    unsigned randomSeed = 7;

    // The breakout
    double stopAtrFraction = 0.1;
    double riskPercent = 1.0;
    double maxLeverage = 4.0;

    // Costs
    double startingCapital = 100000;
    double slippagePercent = 0.03;

    std::vector<Variant> variants;

    // Pass marks
    double minMoveToCost = 3.0;
    double minSharpe = 1.0;            // annualised
    double minLeadOverRandom = 0.5;    // Sharpe ahead of the same rule on random stocks
    int minTrades = 300;
    double minDeflatedSharpe = 0.95;

    // From config/morning.json's contents; `end` empty -> `yesterday`
    static MorningSettings fromConfig(const json& config, const std::string& yesterday);

    double slippageFraction() const { return slippagePercent / 100.0; }

    // A position's largest value: max_leverage x equity shared between the day's stocks
    double maxPositionValue(double equity) const { return maxLeverage * equity / stocks; }

    // DEVELOPMENT, VALIDATION or HOLDOUT
    std::string periodOf(const std::string& day) const;
};
