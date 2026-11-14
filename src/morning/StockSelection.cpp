#include "StockSelection.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <random>

namespace
{
    // FNV-1a: the same number for the same day on every machine (std::hash makes no such promise)
    const uint64_t FNV_OFFSET = 1469598103934665603ULL;
    const uint64_t FNV_PRIME = 1099511628211ULL;

    uint64_t stableHash(const std::string& text)
    {
        uint64_t hash = FNV_OFFSET;
        for (unsigned char c : text)
        {
            hash ^= c;
            hash *= FNV_PRIME;
        }
        return hash;
    }

    // In the universe, priced above min_price at the open, and an opening bar that moved
    bool tradeableToday(const StockSelection::Candidate& candidate, const std::map<std::string, IntradayBar>& openings,
                        const MorningSettings& settings)
    {
        auto opening = openings.find(candidate.symbol);
        return opening != openings.end() && opening->second.open > settings.minPrice
            && StockSelection::directionOf(opening->second) != 0;
    }

    StockSelection::Pick pickOf(const StockSelection::Candidate& candidate, const IntradayBar& opening,
                                double relativeVolume)
    {
        return {candidate.symbol, candidate.atr, relativeVolume, StockSelection::directionOf(opening)};
    }
}

namespace StockSelection
{
    double averageTrueRange(const std::vector<quant::DailyBar>& bars, size_t before, int days)
    {
        if (days <= 0 || before < static_cast<size_t>(days) + 1 || before > bars.size())
            return 0;
        double total = 0;
        for (size_t i = before - days; i < before; i++)
        {
            double previousClose = bars[i - 1].close;
            total += std::max({bars[i].high - bars[i].low, std::fabs(bars[i].high - previousClose),
                               std::fabs(bars[i].low - previousClose)});
        }
        return total / days;
    }

    void addCandidates(const std::string& symbol, const std::vector<quant::DailyBar>& bars,
                       const MorningSettings& settings, const std::string& firstDay, CandidatesByDay& byDay)
    {
        size_t lookback = static_cast<size_t>(settings.lookbackDays);
        for (size_t day = lookback + 1; day < bars.size(); day++)
        {
            if (bars[day].date < firstDay || bars[day].date > settings.end)
                continue;
            double previousClose = bars[day - 1].close;
            if (previousClose <= settings.minPrice)
                continue;
            double volume = 0;
            for (size_t i = day - lookback; i < day; i++)
                volume += bars[i].volume;
            double averageVolume = volume / lookback;
            if (averageVolume < settings.minAverageVolume)
                continue;
            double atr = averageTrueRange(bars, day, settings.lookbackDays);
            if (atr < settings.minAtr)
                continue;
            byDay[bars[day].date].push_back({symbol, atr, averageVolume});
        }
    }

    int directionOf(const IntradayBar& opening)
    {
        if (opening.close > opening.open)
            return 1;
        if (opening.close < opening.open)
            return -1;
        return 0;
    }

    double OpeningVolumes::relativeVolume(const std::string& symbol, double volume) const
    {
        auto history = recent.find(symbol);
        if (history == recent.end() || static_cast<int>(history->second.size()) < settings.minHistoryDays)
            return 0;
        double average = std::accumulate(history->second.begin(), history->second.end(), 0.0)
                       / history->second.size();
        return average > 0 ? volume / average : 0;
    }

    void OpeningVolumes::add(const std::map<std::string, IntradayBar>& openings)
    {
        for (const auto& [symbol, opening] : openings)
        {
            auto& history = recent[symbol];
            history.push_back(opening.volume);
            while (static_cast<int>(history.size()) > settings.lookbackDays)
                history.pop_front();
        }
    }

    std::vector<Pick> inPlay(const std::vector<Candidate>& candidates, const std::map<std::string, IntradayBar>& openings,
                             const OpeningVolumes& volumes, const MorningSettings& settings)
    {
        std::vector<Pick> picks;
        for (const auto& candidate : candidates)
        {
            if (!tradeableToday(candidate, openings, settings))
                continue;
            const IntradayBar& opening = openings.at(candidate.symbol);
            double relative = volumes.relativeVolume(candidate.symbol, opening.volume);
            if (relative > 0 && relative >= settings.minRelativeVolume)
                picks.push_back(pickOf(candidate, opening, relative));
        }
        std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) {
            return a.relativeVolume != b.relativeVolume ? a.relativeVolume > b.relativeVolume : a.symbol < b.symbol;
        });
        if (picks.size() > static_cast<size_t>(settings.stocks))
            picks.resize(settings.stocks);
        return picks;
    }

    std::vector<Pick> randomControl(const std::vector<Candidate>& candidates,
                                    const std::map<std::string, IntradayBar>& openings,
                                    const OpeningVolumes& volumes, const MorningSettings& settings,
                                    const std::string& day)
    {
        std::vector<Candidate> pool;
        for (const auto& candidate : candidates)
            if (tradeableToday(candidate, openings, settings))
                pool.push_back(candidate);
        std::sort(pool.begin(), pool.end(), [](const Candidate& a, const Candidate& b) { return a.symbol < b.symbol; });
        std::mt19937_64 random(stableHash(day) ^ settings.randomSeed);
        std::shuffle(pool.begin(), pool.end(), random);
        if (pool.size() > static_cast<size_t>(settings.stocks))
            pool.resize(settings.stocks);

        std::vector<Pick> picks;
        for (const auto& candidate : pool)
        {
            const IntradayBar& opening = openings.at(candidate.symbol);
            picks.push_back(pickOf(candidate, opening, volumes.relativeVolume(candidate.symbol, opening.volume)));
        }
        return picks;
    }
}
