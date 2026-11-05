#include "ReplayStream.hpp"
#include "../util/MarketTime.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include "MinuteBars.hpp"

namespace fs = std::filesystem;

ReplayStream::ReplayStream(const std::string& dataRoot, const std::vector<std::string>& tickers, std::string date,
                           int days, double speed)
    : dataRoot(dataRoot), tickers(tickers), firstDay(std::move(date)), dayCount(std::max(1, days)), speed(speed)
{
}

std::string
ReplayStream::folderFor(const std::string& dataRoot, const std::string& ticker)
{
    fs::path own = fs::path(dataRoot) / "minute" / ticker;
    return fs::is_directory(own) ? own.string() : dataRoot;
}

std::vector<std::string>
ReplayStream::savedDays(const std::string& dataRoot, const std::string& ticker)
{
    std::vector<std::string> found;
    std::string prefix = "marketData_" + ticker + "_";
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(folderFor(dataRoot, ticker), error))
    {
        std::string name = entry.path().filename().string();
        // marketData_<TICKER>_YYYY-MM-DD.csv
        const std::string extension = ".csv";
        bool isDayFile = name.rfind(prefix, 0) == 0 && name.size() == prefix.size() + MarketTime::DATE_LENGTH + extension.size()
                      && name.compare(name.size() - extension.size(), extension.size(), extension) == 0;
        if (isDayFile)
            found.push_back(name.substr(prefix.size(), MarketTime::DATE_LENGTH));
    }
    std::sort(found.begin(), found.end());
    return found;
}

bool
ReplayStream::load()
{
    // The trading days every ticker has saved
    std::set<std::string> common;
    for (size_t i = 0; i < tickers.size(); i++)
    {
        std::vector<std::string> saved = savedDays(dataRoot, tickers[i]);
        if (saved.empty())
        {
            std::cerr << "ERROR - No saved minute bars for " << tickers[i] << " (looked in "
                      << folderFor(dataRoot, tickers[i]) << "). Download them with: ./build/app/screen_app --symbols "
                      << tickers[i] << " --download-only" << std::endl;
            return false;
        }
        std::set<std::string> these(saved.begin(), saved.end());
        if (i == 0)
            common = these;
        else
        {
            std::set<std::string> both;
            std::set_intersection(common.begin(), common.end(), these.begin(), these.end(), std::inserter(both, both.begin()));
            common = both;
        }
    }
    if (common.empty())
    {
        std::cerr << "ERROR - The portfolio's stocks have no saved day in common" << std::endl;
        return false;
    }

    // Which days: from firstDay (or the latest common one), dayCount of them
    std::vector<std::string> all(common.begin(), common.end());
    auto from = firstDay.empty() ? all.end() - 1 : std::lower_bound(all.begin(), all.end(), firstDay);
    if (from == all.end())
    {
        std::cerr << "ERROR - No saved day on or after " << firstDay << " for every stock (latest: " << all.back()
                  << ")" << std::endl;
        return false;
    }
    if (!firstDay.empty() && *from != firstDay)
        std::cout << "No saved bars for every stock on " << firstDay << "; replaying from " << *from << std::endl;
    days.assign(from, std::min(from + dayCount, all.end()));

    std::string historyStart = from - all.begin() >= HISTORY_DAYS ? *(from - HISTORY_DAYS) : all.front();

    for (const auto& ticker : tickers)
    {
        std::vector<MarketCondition> bars = MinuteBars::load(folderFor(dataRoot, ticker), ticker, historyStart, days.back(), LOADING_THREADS);
        for (const auto& bar : bars)
        {
            std::string day = MarketTime::datePart(bar.DateTime);
            if (day < days.front())
                before[ticker].push_back(bar);
            else if (std::binary_search(days.begin(), days.end(), day))
                byMinute[bar.DateTime].push_back(bar);
        }
    }

    // Each minute's bars in portfolio order
    for (auto& [minute, bars] : byMinute)
        std::stable_sort(bars.begin(), bars.end(), [&](const MarketCondition& a, const MarketCondition& b) {
            return std::find(tickers.begin(), tickers.end(), a.Ticker) < std::find(tickers.begin(), tickers.end(), b.Ticker);
        });
    return !byMinute.empty();
}

std::vector<MarketCondition>
ReplayStream::history(const std::string& ticker, int count) const
{
    auto found = before.find(ticker);
    if (found == before.end() || count <= 0)
        return {};
    const auto& bars = found->second;
    size_t take = std::min(bars.size(), static_cast<size_t>(count));
    return std::vector<MarketCondition>(bars.end() - take, bars.end());
}

void
ReplayStream::start(BarCallback onBar, std::function<void()> onFinished)
{
    stop();
    running = true;
    worker = std::thread([this, onBar, onFinished] {
        auto pause = speed > 0 ? std::chrono::microseconds(static_cast<long long>(1e6 / speed))
                               : std::chrono::microseconds(0);
        for (const auto& [minute, bars] : byMinute)
        {
            if (!running)
                return;
            for (const auto& bar : bars)
                onBar(bar);
            if (pause.count() > 0)
                std::this_thread::sleep_for(pause);
        }
        if (running && onFinished)
            onFinished();
    });
}

void
ReplayStream::stop()
{
    running = false;
    if (worker.joinable())
    {
        if (worker.get_id() == std::this_thread::get_id())
            worker.detach();
        else
            worker.join();
    }
}
