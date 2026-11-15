#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "PriceMatrix.hpp"
#include "../morning/MorningBacktest.hpp"
#include "../morning/ResearchProgress.hpp"

using json = nlohmann::json;

/**
 * DailyPrices
 *
 * The daily bars the daily-bar studies (pairs_app, swing_app) share, kept in data/pairs/: every listed US
 * stock trading enough to matter, downloaded once from `from` (later runs only fetch new days), and lined up on
 * SPY's trading days. Which stocks are worth downloading comes from the morning screen's daily bars when it
 * has some (data/morning/daily), so a few thousand are fetched instead of twelve thousand.
 */
namespace DailyPrices
{
    struct Request
    {
        std::string from, to;              // dates
        double downloadAbove = 5e6;        // average daily dollar volume to download a stock at all
        double loadAbove = 0;              // ... and to load it for this study (over all its bars)
        bool offline = false;
        std::vector<std::string> alwaysInclude;   // downloaded and loaded whatever they trade (SPY always is)
    };

    // False (with a message on stderr) if there's no list of stocks or no SPY. `downloads`: requests made.
    bool load(const json& algoConfig, const std::string& dataRoot, const Request& request, ResearchProgress& progress,
              PriceMatrix& prices, size_t& downloads);

    // SPY bought at the close before `first` and held to `last`, for comparison (empty if there's no SPY)
    MorningBacktest::Line buyAndHold(const PriceMatrix& prices, size_t first, size_t last, double capital);

    // The last day index on or before `date` (0 if none)
    size_t indexOnOrBefore(const PriceMatrix& prices, const std::string& date);

    static inline const std::string BENCHMARK = "SPY";
}
