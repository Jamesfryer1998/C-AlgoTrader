#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * ReplayDownloader
 *
 * Before a replay of a chosen day (--replay 2026-10-15 [--days 3]), downloads from Alpaca any minute bars
 * that aren't saved yet: the replayed days, plus the few days before them that strategies are warmed up on.
 * Each stock is checked on its own, and only a stock missing a weekday in that range is downloaded (days
 * already saved are skipped by the download itself).
 *
 * Holidays aren't known, so a stock whose only gap is a market holiday gets a quick check against Alpaca
 * each time; nothing is downloaded for it.
 */
class ReplayDownloader
{
    public:
        ReplayDownloader(const json& config, const std::string& dataRoot);

        // False if a needed download failed (e.g. Alpaca unreachable or no keys)
        bool downloadMissing(const std::vector<std::string>& tickers, const std::string& firstDay, int days);

    private:
        // Where a stock's minute bars are kept: the folder it already uses, or data/minute/<TICKER>/
        std::string folderFor(const std::string& ticker) const;

        json config;
        std::string dataRoot;
};
