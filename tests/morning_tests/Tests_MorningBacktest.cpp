#include <gtest/gtest.h>
#include <filesystem>
#include <sstream>
#include "FakeMarket.hpp"
#include "MorningTestBars.hpp"
#include "morning/MorningBacktest.hpp"
#include "morning/MorningReport.hpp"
#include "morning/OpeningRangeBreakout.hpp"
#include "morning/ScalpDay.hpp"

namespace fs = std::filesystem;
using namespace MorningTestBars;

namespace
{
    const json FLAT_AT_CLOSE = {{"end_of_day", {{"flat_at_close", true}, {"no_entries_minutes_before_close", 15},
                                                 {"close_minutes_before_close", 5}}},
                                {"max_position_size", 10}, {"max_exposure", 5.0}, {"slippage_tolerance", 5.0}};

    // Three weeks: development to 03-05, validation to 03-11, then a holdout to 03-13
    json morningConfig()
    {
        return {{"start", "2026-03-04"}, {"development_end", "2026-03-05"}, {"validation_end", "2026-03-11"},
                {"end", "2026-03-13"},
                {"universe", {{"min_price", 5.0}, {"min_average_volume", 1000000}, {"min_atr", 0.5},
                              {"lookback_days", 3}, {"min_history_days", 1}}},
                {"selection", {{"stocks", 2}, {"min_relative_volume", 1.0}, {"opening_minutes", 5}, {"random_seed", 7}}},
                {"orb", {{"stop_atr_fraction", 0.1}, {"risk_percent", 1.0}, {"max_leverage", 4.0}, {"entry_fill", "next_open"}}},
                {"starting_capital", 100000}, {"slippage_percent", 0.0},
                {"variants", {{{"name", "ORB"}}, {{"name", "ORB, 10-minute hold"}, {"hold_minutes", 10}}}}};
    }

    // UP rises through its opening range and keeps going; DOWN does the reverse; SPY and FLAT don't move
    std::shared_ptr<FakeMarket> trendingMarket()
    {
        auto market = std::make_shared<FakeMarket>(std::vector<std::string>{"DOWN", "FLAT", "SPY", "UP"});
        market->opening = [](const std::string& symbol) {
            if (symbol == "UP")
                return std::vector<double>{50, 51, 50, 51, 5000};
            if (symbol == "DOWN")
                return std::vector<double>{50, 50, 49, 49, 5000};
            return std::vector<double>{50, 50.2, 49.9, 50, 1000};   // didn't move: can't be picked
        };
        market->session = [](const std::string& symbol, int minute) {
            double move = minute < 5 ? 0 : (minute - 5) * 0.01;   // a cent a minute after the opening range
            if (symbol == "UP")
                return minute < 5 ? std::vector<double>{50, 51, 50, 51} : std::vector<double>{51 + move, 51.02 + move, 51 + move, 51.01 + move};
            if (symbol == "DOWN")
                return minute < 5 ? std::vector<double>{50, 50, 49, 49} : std::vector<double>{49 - move, 49 - move, 48.98 - move, 48.99 - move};
            return std::vector<double>{50, 50, 50, 50};
        };
        return market;
    }

    class MorningBacktestTests : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                folder = (fs::temp_directory_path() / ("morning_backtest_" + std::to_string(::getpid()))).string();
                fs::remove_all(folder);
            }
            void TearDown() override { fs::remove_all(folder); }

            MorningBacktest::Results run(bool holdout)
            {
                MorningSettings settings = MorningSettings::fromConfig(morningConfig(), "2026-03-13");
                MorningData data(json::object(), folder, false, market);
                MorningBacktest backtest(FLAT_AT_CLOSE, settings);
                MorningBacktest::Results results;
                EXPECT_TRUE(backtest.run(data, holdout, nullptr, results));
                return results;
            }

            const MorningBacktest::Line& line(const MorningBacktest::Results& results, const std::string& variant,
                                              const std::string& selection)
            {
                for (const auto& candidate : results.lines)
                    if (candidate.variant == variant && candidate.selection == selection)
                        return candidate;
                ADD_FAILURE() << "no line " << variant << " / " << selection;
                return results.lines.front();
            }

            std::string folder;
            std::shared_ptr<FakeMarket> market = trendingMarket();
    };
}

TEST_F(MorningBacktestTests, TradesTheStocksInPlayEachDayAndLeavesTheHoldoutAlone)
{
    MorningBacktest::Results results = run(false);
    EXPECT_EQ(results.symbols, 4);
    ASSERT_EQ(results.lines.size(), 5u);   // 2 variants x {in play, random} + SPY
    EXPECT_FALSE(results.holdoutTraded);
    EXPECT_EQ(results.daysTraded, 6) << "03-04 to 03-11";

    const auto& orb = line(results, "ORB", MorningBacktest::IN_PLAY);
    EXPECT_EQ(orb.days.size(), 6u);
    ASSERT_EQ(orb.trades.size(), 12u) << "UP and DOWN every day";
    int longs = 0;
    for (const auto& trade : orb.trades)
    {
        EXPECT_GT(trade.netPnl, 0) << trade.symbol << " " << trade.day;
        EXPECT_EQ(trade.exitReason, OpeningRangeBreakout::CLOSE);
        longs += trade.side > 0;
        EXPECT_EQ(trade.symbol, trade.side > 0 ? "UP" : "DOWN");
    }
    EXPECT_EQ(longs, 6);
    EXPECT_GT(orb.equity, 100000);
    EXPECT_EQ(orb.trades.front().day, "2026-03-04");

    // Step 1 measured both selections, over development and validation
    ASSERT_TRUE(results.moves.count(MorningBacktest::IN_PLAY));
    EXPECT_EQ(results.moves.at(MorningBacktest::IN_PLAY).stockDays, 12);
    EXPECT_NEAR(results.moves.at(MorningBacktest::IN_PLAY).move10Percent, 0.1 / 51 * 100, 0.01);

    // The holdout's bars are downloaded now, so the final check needs no downloads
    size_t asked = market->requests.size();
    MorningBacktest::Results withHoldout = run(true);
    EXPECT_TRUE(withHoldout.holdoutTraded);
    EXPECT_EQ(withHoldout.daysTraded, 8);
    EXPECT_EQ(market->requests.size(), asked) << "everything came from the saved files";
}

TEST_F(MorningBacktestTests, TheReportJudgesEachVariantAndSavesItsCurves)
{
    MorningBacktest::Results results = run(false);
    MorningSettings settings = MorningSettings::fromConfig(morningConfig(), "2026-03-13");
    MorningReport report(settings, results);

    bool sawValidation = false;
    for (const auto& row : report.rows())
    {
        EXPECT_NE(row.period, MorningSettings::HOLDOUT);
        if (row.variant == "ORB" && row.selection == MorningBacktest::IN_PLAY && row.period == MorningSettings::VALIDATION)
        {
            sawValidation = true;
            EXPECT_EQ(row.days, 4);
            EXPECT_EQ(row.trades, 8);
            EXPECT_DOUBLE_EQ(row.winRate, 1.0);
            EXPECT_GT(row.totalReturn, 0);
            EXPECT_GE(row.deflatedSharpe, 0);
        }
    }
    EXPECT_TRUE(sawValidation);

    ASSERT_FALSE(report.gates().empty());
    EXPECT_EQ(report.gates().front().step.rfind("Step 1", 0), 0u);
    bool sawStep3 = false;
    for (const auto& gate : report.gates())
        if (gate.variant == "ORB")
        {
            sawStep3 = true;
            EXPECT_EQ(gate.period, MorningSettings::VALIDATION);
            EXPECT_EQ(gate.checks.size(), 3u) << "Sharpe, lead over random, earns more than it costs";
        }
    EXPECT_TRUE(sawStep3);

    json saved = report.toJson("", 42);
    EXPECT_TRUE(saved["holdout_opened"].is_null());
    EXPECT_EQ(saved["downloads"], 42);
    EXPECT_EQ(saved["curves"].size(), 5u);
    EXPECT_EQ(saved["curves"][0]["points"].size(), 6u);
    std::ostringstream printed;
    report.print(printed);
    EXPECT_NE(printed.str().find("GATES"), std::string::npos);
}

TEST_F(MorningBacktestTests, TheHoldoutIsRecordedTheFirstTimeOnly)
{
    EXPECT_EQ(MorningHoldout::openedAt(folder), "");
    std::string first = MorningHoldout::open(folder, {"ORB"});
    EXPECT_FALSE(first.empty());
    EXPECT_EQ(MorningHoldout::openedAt(folder), first);
    EXPECT_EQ(MorningHoldout::open(folder, {"ORB", "SCALP"}), first);
}

TEST(MedianMoveTests, MeasuresWindowsFromTheEndOfTheOpeningRange)
{
    std::vector<IntradayBar> bars = flatDay("2026-03-02", 100);
    setBar(bars, minute(9, 45), 101, 101, 101, 101);   // 09:35 -> 09:45: +1%
    double move = MorningBacktest::medianMovePercent(bars, 10, minute(9, 35));
    EXPECT_DOUBLE_EQ(move, 0) << "one window moved, most didn't";
    for (int m = minute(9, 45); m < minute(16, 0); m += 20)
        setBar(bars, m, 101, 101, 101, 101);           // every other window now moves 1%
    EXPECT_NEAR(MorningBacktest::medianMovePercent(bars, 10, minute(9, 35)), 0.5, 0.5);
}

TEST(ScalpDayTests, RunsTheLiveStrategyOnAPickedStockWithItsCosts)
{
    // Quiet around 100 for half an hour, then a sharp drop to 98 that snaps back: SCALP's VWAP fade
    std::vector<IntradayBar> bars = flatDay("2026-03-02", 100);
    for (int m = minute(9, 30); m < minute(10, 0); m++)
    {
        double wiggle = (m % 2 ? 0.05 : -0.05);
        setBar(bars, m, 100 + wiggle, 100.1, 99.9, 100 - wiggle);
    }
    setBar(bars, minute(10, 30), 100, 100, 98, 98);
    setBar(bars, minute(10, 31), 98, 99, 98, 99);
    for (int m = minute(10, 32); m < minute(16, 0); m++)
        setBar(bars, m, 100, 100, 100, 100);

    MorningSettings settings = simpleSettings();
    json scalp = {{"active", 1}, {"name", "SCALP"}, {"entry_threshold", 2.0}, {"exit_threshold", 0.0},
                  {"max_hold_bars", 30}, {"warmup_minutes", 15}, {"stop_loss", 5.0}, {"take_profit", 5.0}};
    TradingCosts costs;
    costs.slippagePercent = 0.03;
    ScalpDay day(FLAT_AT_CLOSE, scalp, settings, costs);
    StockDay result = day.run({"KO", 1.0, 2.0, 1}, bars, 10000);
    EXPECT_EQ(result.day, "2026-03-02");
    EXPECT_GE(result.trades, 1);
    EXPECT_GT(result.costs, 0);
    EXPECT_NEAR(result.netPnl, result.grossPnl - result.costs, 1e-6);
}
