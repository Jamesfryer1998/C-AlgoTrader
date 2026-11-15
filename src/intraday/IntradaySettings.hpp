#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../morning/MorningSettings.hpp"

using json = nlohmann::json;

/**
 * IntradaySettings
 *
 * intraday_app's rules, from config/intraday.json (see its comments): the dates, how each month's stocks and
 * their sector funds are picked, when in the day it trades, how far a stock has to move from its sector to be
 * traded, money and costs, and the variants tried.
 */
struct IntradaySettings
{
    enum class Direction { Fade, Ride };   // bet the gap to the sector closes, or that it keeps growing

    struct Variant
    {
        std::string name;
        Direction direction = Direction::Fade;
        int lookbackMinutes = 30;   // the gap: the stock's move less beta x its fund's, over this many minutes
        int holdMinutes = 30;       // out after this long if the gap hasn't closed
        int delayMinutes = 1;       // filled at the open of the minute this many after the signal (0: its close)
        bool hedge = true;          // hold -beta x each stock in its fund
    };

    static inline const std::string FADE_STEP = "Fading the gap to the sector";
    static inline const std::string RIDE_STEP = "Riding the gap to the sector";

    std::string start, developmentEnd, validationEnd, end, dataStart;

    // Each month's stocks
    size_t stocks = 100;
    int formationDays = 252;
    double minPrice = 5.0;
    double maxCorrelation = 0.9;               // a fund, an index tracker or another class of a stock already in
    double prefilterDollarVolume = 5e6;
    double loadDollarVolume = 100e6;
    std::vector<std::string> funds;            // the sector funds, each stock hedged in its closest
    std::vector<std::string> notStocks;        // funds of other things (gold, bonds, bitcoin) never traded

    // The day, in minutes after the open
    int firstMinute = 30;
    int lastEntryMinute = 360;
    int stepMinutes = 5;

    size_t stocksEachSide = 10;
    double entryZ = 2.0;                       // the gap, in its usual size over the lookback, to trade it
    double exitZ = 0.0;                        // ... and to call it closed

    double startingCapital = 100000;
    double slippagePercent = 0.03;
    double leverage = 1.0;
    unsigned randomSeed = 7;

    std::vector<Variant> variants;
    json gates;

    static IntradaySettings fromConfig(const json& config, const std::string& yesterday);

    // What the shared research report needs: the dates, costs, pass marks and each variant's step
    MorningSettings forReport() const;
};
