#pragma once

#include <deque>
#include <map>
#include <string>
#include <vector>
#include "MorningSettings.hpp"
#include "../data_access/IntradayBar.hpp"
#include "../quant/DailyBars.hpp"

/**
 * StockSelection
 *
 * Which stocks the morning screen trades each day, using only what was known by 09:35 New York time:
 *
 *   1. The universe (from the 14 trading days before): price, average volume and ATR high enough
 *   2. Relative volume: the first 5 minutes' volume today / its average over the last 14 days' first 5 minutes
 *   3. In play: the `stocks` with the highest relative volume (at least min_relative_volume) whose opening bar
 *      moved up or down. The control: the same number picked at random from the universe that day.
 */
namespace StockSelection
{
    // A stock that can be picked on a day, with what the days before it say about it
    struct Candidate
    {
        std::string symbol;
        double atr = 0;              // 14-day average true range, $
        double averageVolume = 0;    // shares a day
    };

    // A stock picked for a day
    struct Pick
    {
        std::string symbol;
        double atr = 0;
        double relativeVolume = 0;   // 0 when unknown (a random pick without enough history)
        int direction = 0;           // +1 the opening bar rose (trade long), -1 it fell (trade short)
    };

    using CandidatesByDay = std::map<std::string, std::vector<Candidate>>;

    // The average true range of the `days` days before index `before` (oldest first). 0 if too few.
    double averageTrueRange(const std::vector<quant::DailyBar>& bars, size_t before, int days);

    // Add each day (from `firstDay`) on which this stock passes the universe filters, judged on the days before
    void addCandidates(const std::string& symbol, const std::vector<quant::DailyBar>& bars,
                       const MorningSettings& settings, const std::string& firstDay, CandidatesByDay& byDay);

    // +1 if the bar closed above its open, -1 below, 0 unchanged
    int directionOf(const IntradayBar& opening);

    // The first minutes' volume of each stock over the last lookback_days days it had one
    class OpeningVolumes
    {
        public:
            explicit OpeningVolumes(const MorningSettings& settings) : settings(settings) {}

            // Today's volume against the recent average; 0 without min_history_days of history
            double relativeVolume(const std::string& symbol, double volume) const;

            // Remember a day's opening volumes (after that day's picks were made)
            void add(const std::map<std::string, IntradayBar>& openings);

        private:
            MorningSettings settings;
            std::map<std::string, std::deque<double>> recent;
    };

    // The day's stocks in play, highest relative volume first
    std::vector<Pick> inPlay(const std::vector<Candidate>& candidates, const std::map<std::string, IntradayBar>& openings,
                             const OpeningVolumes& volumes, const MorningSettings& settings);

    // The same number of stocks drawn at random (repeatably, from the day and the seed) from those that
    // could have been picked: in the universe, priced above min_price at the open, an opening bar that moved
    std::vector<Pick> randomControl(const std::vector<Candidate>& candidates,
                                    const std::map<std::string, IntradayBar>& openings,
                                    const OpeningVolumes& volumes, const MorningSettings& settings,
                                    const std::string& day);
}
