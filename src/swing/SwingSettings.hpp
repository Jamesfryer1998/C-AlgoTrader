#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../morning/MorningSettings.hpp"

using json = nlohmann::json;

/**
 * SwingSettings
 *
 * swing_app's rules, from config/swing.json (see its comments): the dates, which stocks can be bought each day,
 * the three signals (a pullback in an uptrend, a breakout to a 52-week high, the week's biggest losers), how many
 * positions at once, money and costs, and the variants tried.
 */
struct SwingSettings
{
    enum class Kind { Pullback, Breakout, Losers };
    enum class Entry { NextOpen, SignalClose };

    struct Variant
    {
        std::string name;
        Kind kind = Kind::Pullback;
        Entry entry = Entry::NextOpen;
        std::vector<std::string> symbols;   // only these (empty: any stock that trades enough)
        size_t positions = 0;               // at once (0: the shared `positions`)
    };

    static inline const std::string PULLBACK_STEP = "Pullbacks in an uptrend";
    static inline const std::string BREAKOUT_STEP = "Breakouts to a 52-week high";
    static inline const std::string LOSERS_STEP = "The week's biggest losers";

    std::string start, developmentEnd, validationEnd, end, dataStart;

    // Which stocks can be bought on a day
    double minPrice = 5.0;
    double minDollarVolume = 20e6;
    int dollarVolumeDays = 20;
    double prefilterDollarVolume = 5e6;
    double loadDollarVolume = 10e6;   // only stocks averaging this over all their bars are loaded (memory)

    // Pullback in an uptrend (Connors RSI(2))
    int trendDays = 200;          // the close above its average over this many days
    int rsiDays = 2;
    double rsiBelow = 10;
    int exitAboveDays = 5;        // out at the next open once the close is above its average over this many days
    int pullbackMaxHold = 10;     // ... or at the close after this many days

    // Breakout to a 52-week high
    int highDays = 252;           // the close at least the highest close of the days before, over this many
    int volumeDays = 50;
    double volumeRatio = 1.5;     // volume at least this times its average over volume_days
    int atrDays = 14;
    double stopAtr = 2.0;         // stop this many ATRs under the entry
    int breakoutHold = 20;

    // The week's biggest losers, bought at the week's last close for a bounce
    int returnDays = 5;
    int losersHold = 5;

    size_t positions = 20;
    double startingCapital = 100000;
    double slippagePercent = 0.03;
    double leverage = 1.0;
    unsigned randomSeed = 7;

    std::vector<Variant> variants;
    json gates;

    static SwingSettings fromConfig(const json& config, const std::string& yesterday);

    size_t positionsOf(const Variant& variant) const { return variant.positions ? variant.positions : positions; }

    // What the shared research report needs: the dates, costs, pass marks and each variant's step
    MorningSettings forReport() const;
};
