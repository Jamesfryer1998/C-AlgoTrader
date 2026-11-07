#include "ReplayDownloader.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <set>
#include "../alpaca/AlpacaHistory.hpp"
#include "../backtest/ReplayStream.hpp"
#include "../util/MarketTime.hpp"

namespace
{
    // New stocks' minute bars go in data/minute/<TICKER>/ (where screen_app keeps them)
    const std::string MINUTE_FOLDER = "minute";
}

ReplayDownloader::ReplayDownloader(const json& config, const std::string& dataRoot)
    : config(config), dataRoot(dataRoot)
{
}

bool
ReplayDownloader::downloadMissing(const std::vector<std::string>& tickers, const std::string& firstDay, int days)
{
    std::string today = MarketTime::datePart(MarketTime::toNewYorkTime(std::time(nullptr)));
    std::string historyStart = MarketTime::addWeekdays(firstDay, -ReplayStream::HISTORY_DAYS);
    std::string lastDay = std::min(MarketTime::addWeekdays(firstDay, std::max(days, 1) - 1), today);
    if (lastDay < historyStart)
        return true;
    std::vector<std::string> needed = MarketTime::weekdaysBetween(historyStart, lastDay);

    for (const auto& ticker : tickers)
    {
        std::vector<std::string> saved = ReplayStream::savedDays(dataRoot, ticker);
        std::set<std::string> have(saved.begin(), saved.end());
        bool missing = std::any_of(needed.begin(), needed.end(), [&](const std::string& day) { return !have.count(day); });
        if (!missing)
            continue;

        std::cout << "Downloading " << ticker << " minute bars " << historyStart << " to " << lastDay
                  << " from Alpaca for the replay..." << std::endl;
        json stockConfig = config;
        stockConfig["ticker"] = ticker;
        AlpacaHistory history(stockConfig);
        history.setQuiet(true);
        if (history.downloadToCsv(historyStart, lastDay, folderFor(ticker)) < 0)
        {
            std::cerr << "ERROR - Could not download " << ticker << "'s bars (check .env and the connection)" << std::endl;
            return false;
        }
    }
    return true;
}

std::string
ReplayDownloader::folderFor(const std::string& ticker) const
{
    bool hasSavedBars = !ReplayStream::savedDays(dataRoot, ticker).empty();
    if (hasSavedBars)
        return ReplayStream::folderFor(dataRoot, ticker);
    return (std::filesystem::path(dataRoot) / MINUTE_FOLDER / ticker).string();
}
