#include "ReplayDownloader.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include "../alpaca/AlpacaHistory.hpp"
#include "../backtest/ReplayStream.hpp"
#include "../util/MarketTime.hpp"

namespace
{
    // New stocks' minute bars go in data/minute/<TICKER>/ (where screen_app keeps them)
    const std::string MINUTE_FOLDER = "minute";

    // In each stock's folder: weekdays Alpaca returned no bars for, e.g. ["2025-11-27"]
    const std::string DAYS_WITHOUT_BARS_FILE = ".days_without_bars.json";
    const int JSON_INDENT = 2;

    std::set<std::string> readDays(const std::string& path)
    {
        std::set<std::string> days;
        std::ifstream file(path);
        json list = json::parse(file, nullptr, false);
        if (list.is_array())
            for (const auto& day : list)
                if (day.is_string())
                    days.insert(day.get<std::string>());
        return days;
    }
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
        std::set<std::string> have = coveredDays(ticker);
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
        rememberDaysWithoutBars(ticker, needed, today);
    }
    return true;
}

std::set<std::string>
ReplayDownloader::coveredDays(const std::string& ticker) const
{
    std::vector<std::string> saved = ReplayStream::savedDays(dataRoot, ticker);
    std::set<std::string> days(saved.begin(), saved.end());
    std::set<std::string> empty = readDays((std::filesystem::path(folderFor(ticker)) / DAYS_WITHOUT_BARS_FILE).string());
    days.insert(empty.begin(), empty.end());
    return days;
}

// Today may simply not have finished, so only earlier days are remembered
void
ReplayDownloader::rememberDaysWithoutBars(const std::string& ticker, const std::vector<std::string>& needed,
                                          const std::string& today) const
{
    std::string path = (std::filesystem::path(folderFor(ticker)) / DAYS_WITHOUT_BARS_FILE).string();
    std::set<std::string> empty = readDays(path);
    std::vector<std::string> saved = ReplayStream::savedDays(dataRoot, ticker);
    std::set<std::string> have(saved.begin(), saved.end());

    size_t before = empty.size();
    for (const auto& day : needed)
        if (day < today && !have.count(day))
            empty.insert(day);
    if (empty.size() == before)
        return;
    std::ofstream(path) << json(std::vector<std::string>(empty.begin(), empty.end())).dump(JSON_INDENT) << "\n";
}

std::string
ReplayDownloader::folderFor(const std::string& ticker) const
{
    bool hasSavedBars = !ReplayStream::savedDays(dataRoot, ticker).empty();
    if (hasSavedBars)
        return ReplayStream::folderFor(dataRoot, ticker);
    return (std::filesystem::path(dataRoot) / MINUTE_FOLDER / ticker).string();
}
