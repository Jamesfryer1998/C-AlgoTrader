#include <gtest/gtest.h>
#include "MorningTestBars.hpp"
#include "morning/StockSelection.hpp"

using namespace MorningTestBars;
using StockSelection::Candidate;
using StockSelection::Pick;

namespace
{
    // `days` daily bars from 2026-01-01 on, each with this range, close and volume
    std::vector<quant::DailyBar> dailyBars(int days, double close, double range, double volume)
    {
        std::vector<quant::DailyBar> bars;
        for (int i = 0; i < days; i++)
            bars.push_back({"2026-01-" + twoDigits(i + 1), close, close + range / 2, close - range / 2, close, volume});
        return bars;
    }

    MorningSettings universeSettings()
    {
        MorningSettings settings = simpleSettings();
        settings.lookbackDays = 3;
        settings.end = "2026-12-31";
        return settings;
    }

    IntradayBar opening(double open, double close, double volume)
    {
        return bar("2026-03-02", minute(9, 30), open, std::max(open, close), std::min(open, close), close, volume);
    }
}

TEST(StockSelectionTests, AverageTrueRangeUsesTheDaysBeforeAndPreviousCloses)
{
    std::vector<quant::DailyBar> bars = {{"d1", 10, 11, 9, 10, 0}, {"d2", 10, 11, 9, 10, 0},
                                         {"d3", 14, 15, 13, 14, 0}, {"d4", 14, 14.5, 13.5, 14, 0}};
    // d2: range 2; d3: 15 - previous close 10 = 5
    EXPECT_DOUBLE_EQ(StockSelection::averageTrueRange(bars, 3, 2), (2.0 + 5.0) / 2);
    EXPECT_DOUBLE_EQ(StockSelection::averageTrueRange(bars, 2, 2), 0) << "too few days before";
}

TEST(StockSelectionTests, TheUniverseIsJudgedOnTheDaysBeforeOnly)
{
    MorningSettings settings = universeSettings();
    StockSelection::CandidatesByDay byDay;
    StockSelection::addCandidates("LIQUID", dailyBars(8, 50, 2, 2e6), settings, "2026-01-01", byDay);
    StockSelection::addCandidates("CHEAP", dailyBars(8, 4, 2, 2e6), settings, "2026-01-01", byDay);
    StockSelection::addCandidates("THIN", dailyBars(8, 50, 2, 1e5), settings, "2026-01-01", byDay);
    StockSelection::addCandidates("QUIET", dailyBars(8, 50, 0.2, 2e6), settings, "2026-01-01", byDay);

    ASSERT_TRUE(byDay.count("2026-01-05"));
    EXPECT_FALSE(byDay.count("2026-01-04")) << "the first day needs lookback_days + 1 days before it";
    ASSERT_EQ(byDay["2026-01-05"].size(), 1u);
    EXPECT_EQ(byDay["2026-01-05"][0].symbol, "LIQUID");
    EXPECT_DOUBLE_EQ(byDay["2026-01-05"][0].atr, 2);

    // A volume spike on the day itself isn't known yet that morning
    std::vector<quant::DailyBar> spike = dailyBars(8, 50, 2, 1e5);
    spike[5].volume = 1e9;
    StockSelection::CandidatesByDay withSpike;
    StockSelection::addCandidates("SPIKE", spike, settings, "2026-01-01", withSpike);
    EXPECT_FALSE(withSpike.count("2026-01-06"));
    EXPECT_TRUE(withSpike.count("2026-01-07"));
}

TEST(StockSelectionTests, RelativeVolumeNeedsEnoughHistory)
{
    MorningSettings settings = universeSettings();
    settings.minHistoryDays = 2;
    StockSelection::OpeningVolumes volumes(settings);
    volumes.add({{"KO", opening(10, 11, 100)}});
    EXPECT_DOUBLE_EQ(volumes.relativeVolume("KO", 300), 0) << "one day of history is not enough";
    volumes.add({{"KO", opening(10, 11, 200)}});
    EXPECT_DOUBLE_EQ(volumes.relativeVolume("KO", 300), 2.0);
    for (int i = 0; i < 5; i++)
        volumes.add({{"KO", opening(10, 11, 50)}});
    EXPECT_DOUBLE_EQ(volumes.relativeVolume("KO", 300), 6.0) << "only the last lookback_days count";
}

TEST(StockSelectionTests, InPlayIsTheHighestRelativeVolumeWithAMovingOpen)
{
    MorningSettings settings = universeSettings();
    settings.minHistoryDays = 1;
    settings.minRelativeVolume = 1.5;
    StockSelection::OpeningVolumes volumes(settings);
    volumes.add({{"A", opening(10, 11, 100)}, {"B", opening(10, 11, 100)}, {"C", opening(10, 11, 100)},
                 {"D", opening(10, 11, 100)}, {"E", opening(10, 11, 100)}});
    std::vector<Candidate> candidates = {{"A", 1, 1}, {"B", 1, 1}, {"C", 1, 1}, {"D", 1, 1}, {"E", 1, 1}};
    std::map<std::string, IntradayBar> today = {
        {"A", opening(10, 11, 300)},   // 3x, up
        {"B", opening(10, 9, 500)},    // 5x, down
        {"C", opening(10, 10, 900)},   // 9x but didn't move: no direction
        {"D", opening(10, 11, 120)},   // 1.2x: below the minimum
        {"E", opening(4, 4.5, 800)}};  // 8x but priced under $5
    std::vector<Pick> picks = StockSelection::inPlay(candidates, today, volumes, settings);
    ASSERT_EQ(picks.size(), 2u);
    EXPECT_EQ(picks[0].symbol, "B");
    EXPECT_EQ(picks[0].direction, -1);
    EXPECT_DOUBLE_EQ(picks[0].relativeVolume, 5);
    EXPECT_EQ(picks[1].symbol, "A");
    EXPECT_EQ(picks[1].direction, 1);
}

TEST(StockSelectionTests, TheRandomControlIsRepeatableAndFromTheSamePool)
{
    MorningSettings settings = universeSettings();
    StockSelection::OpeningVolumes volumes(settings);
    std::vector<Candidate> candidates;
    std::map<std::string, IntradayBar> today;
    for (int i = 0; i < 30; i++)
    {
        std::string symbol = "S" + std::to_string(i);
        candidates.push_back({symbol, 1, 1});
        today[symbol] = opening(10, i % 2 ? 11 : 9, 100);
    }
    today["S0"] = opening(10, 10, 100);   // can't be picked: no direction

    auto first = StockSelection::randomControl(candidates, today, volumes, settings, "2026-03-02");
    auto again = StockSelection::randomControl(candidates, today, volumes, settings, "2026-03-02");
    auto nextDay = StockSelection::randomControl(candidates, today, volumes, settings, "2026-03-03");
    ASSERT_EQ(first.size(), 2u);
    EXPECT_EQ(first[0].symbol, again[0].symbol);
    EXPECT_EQ(first[1].symbol, again[1].symbol);
    bool sameAsNextDay = first[0].symbol == nextDay[0].symbol && first[1].symbol == nextDay[1].symbol;
    EXPECT_FALSE(sameAsNextDay) << "a different draw each day";
    for (const auto& pick : first)
        EXPECT_NE(pick.symbol, "S0");
}
