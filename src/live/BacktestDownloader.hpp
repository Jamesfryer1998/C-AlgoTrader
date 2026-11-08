#pragma once

#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * BacktestDownloader
 *
 * Before a backtest of a chosen day (--backtest 2026-10-15 [--days 3]), downloads from Alpaca any minute bars
 * that aren't saved yet: the backtested days, plus the few days before them that strategies are warmed up on.
 * Each stock is checked on its own, and only a stock missing a weekday in that range is downloaded (days
 * already saved are skipped by the download itself).
 *
 * Weekdays Alpaca has no bars for (market holidays like Thanksgiving, or a halted stock) are remembered in
 * the stock's folder (.days_without_bars.json) after a download, so they don't trigger another one.
 */
class BacktestDownloader
{
    public:
        BacktestDownloader(const json& config, const std::string& dataRoot);

        // False if a needed download failed (e.g. Alpaca unreachable or no keys)
        bool downloadMissing(const std::vector<std::string>& tickers, const std::string& firstDay, int days);

    private:
        // Where a stock's minute bars are kept: the folder it already uses, or data/minute/<TICKER>/
        std::string folderFor(const std::string& ticker) const;

        // Days saved for a stock, plus the days already known to have no bars
        std::set<std::string> coveredDays(const std::string& ticker) const;

        // After a download: remember the past days in `needed` that still have no bars
        void rememberDaysWithoutBars(const std::string& ticker, const std::vector<std::string>& needed,
                                     const std::string& today) const;

        json config;
        std::string dataRoot;
};
