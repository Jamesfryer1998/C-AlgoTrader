#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * Portfolio
 *
 * Which stocks the live app trades, and with which strategies. In config/strategy.json:
 *
 *   "portfolio": [
 *       {"ticker": "TXN",  "strategy": {"name": "MEANREV", "lookback_period": 60, ...}},
 *       {"ticker": "WMT",  "strategies": [{"name": "RSI", ...}, {"name": "MACD", ...}]}
 *   ]
 *
 * Each entry has one "strategy" or a list of "strategies". This is the same format as the screen's
 * data/screen/minute/top_picks_config.json, so its contents can be pasted in as they are.
 *
 * Without a "portfolio" (or with an empty one), the app trades the single top-level "ticker" with the
 * top-level "strategies", as before.
 */
struct PortfolioEntry
{
    std::string ticker;
    json config;   // the whole config, with "ticker" and "strategies" set for this stock
};

namespace Portfolio
{
    // Throws std::runtime_error with a readable message if an entry is malformed (no ticker, no strategy,
    // a ticker listed twice)
    std::vector<PortfolioEntry> fromConfig(const json& config);
}
