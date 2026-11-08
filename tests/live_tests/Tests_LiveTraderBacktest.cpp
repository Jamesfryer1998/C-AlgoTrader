#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <thread>
#include "../../src/live/LiveTrader.hpp"
#include "../../src/oms/TradeStore.hpp"

namespace fs = std::filesystem;

/**
 * The live app end to end, offline: a backtest (--backtest) runs the same LiveTrader loop as live trading,
 * on minute bars saved in a temporary data folder, with the simulated broker. Nothing is downloaded: every
 * day the backtest needs (the day itself and the HISTORY_DAYS weekdays before it) is already saved.
 *
 * The bars:
 *   PEP  flat all along (alternating 100.00 / 100.10): never far enough from its average to trade
 *   KO   flat for the warm-up days, then on the backtested day a sudden drop at DROP_MINUTE (MEANREV buys
 *        the dip) and a steady slide after it, so the 1% stop loss sells it a few minutes later
 */
namespace
{
    const std::vector<std::string> WARMUP_DAYS = {"2026-10-19", "2026-10-20", "2026-10-21", "2026-10-22", "2026-10-23"};
    const std::string FIRST_DAY = "2026-10-26";    // a Monday
    const std::string SECOND_DAY = "2026-10-27";
    const int MINUTES_PER_DAY = 390;               // 09:30 to 15:59
    const int DROP_MINUTE = 120;
    const double FLAT_LOW = 100.00, FLAT_HIGH = 100.10;
    const double DROP_TO = 98.0;
    const double SLIDE_PER_MINUTE = 0.001;         // 0.1% a minute after the drop
    const int SHARES = 10;
    const double STOP_LOSS_PERCENT = 1.0;

    using PriceAt = std::function<double(const std::string& day, int minute)>;

    double flat(int minute) { return minute % 2 ? FLAT_HIGH : FLAT_LOW; }

    double dropOnFirstDay(const std::string& day, int minute)
    {
        if (day < FIRST_DAY || minute < DROP_MINUTE)
            return flat(minute);
        if (day > FIRST_DAY)
            return DROP_TO * (1 - SLIDE_PER_MINUTE * (MINUTES_PER_DAY - DROP_MINUTE));   // stays where it ended
        return DROP_TO * (1 - SLIDE_PER_MINUTE * (minute - DROP_MINUTE));
    }

    std::string clock(int minute)
    {
        int sinceMidnight = 9 * 60 + 30 + minute;
        std::ostringstream text;
        text << std::setfill('0') << std::setw(2) << sinceMidnight / 60 << ":" << std::setw(2) << sinceMidnight % 60 << ":00";
        return text.str();
    }

    class LiveTraderBacktestTests : public ::testing::Test
    {
        protected:
            fs::path root;
            std::string dataRoot;   // ends in "/", as the app passes it

            void SetUp() override
            {
                root = fs::temp_directory_path()
                     / ("live_backtest_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
                fs::remove_all(root);
                dataRoot = (root / "data").string() + "/";

                std::vector<std::string> days = WARMUP_DAYS;
                days.push_back(FIRST_DAY);
                days.push_back(SECOND_DAY);
                for (const auto& day : days)
                {
                    saveDay("KO", day, dropOnFirstDay);
                    saveDay("PEP", day, [](const std::string&, int minute) { return flat(minute); });
                }
            }

            void TearDown() override { fs::remove_all(root); }

            void saveDay(const std::string& ticker, const std::string& day, const PriceAt& price)
            {
                fs::path folder = fs::path(dataRoot) / "minute" / ticker;
                fs::create_directories(folder);
                std::ofstream file(folder / ("marketData_" + ticker + "_" + day + ".csv"));
                file << MARKET_DATA_CSV_HEADER << "\n";
                for (int minute = 0; minute < MINUTES_PER_DAY; minute++)
                    file << day << " " << clock(minute) << "," << ticker << "," << price(day, minute) << ","
                         << price(day, minute) << ",1000,1m\n";
            }

            static json config()
            {
                json meanrev = {{"name", "MEANREV"}, {"lookback_period", 10}, {"moving_average_period", 10},
                                {"entry_threshold", 2.0}, {"exit_threshold", 0.0},
                                {"stop_loss", STOP_LOSS_PERCENT}, {"take_profit", 50}};
                return {{"starting_capital", 100000.0},
                        {"max_position_size", SHARES}, {"max_exposure", 5.0}, {"slippage_tolerance", 2.0},
                        {"risk", {{"name", "FIXED_SIZE"}, {"quantity", SHARES}}},
                        {"portfolio", {{{"ticker", "KO"}, {"strategy", meanrev}},
                                       {{"ticker", "PEP"}, {"strategy", meanrev}}}}};
            }

            static LiveOptions backtest(const std::string& day, int days = 1)
            {
                LiveOptions options;
                options.backtest = true;
                options.backtestDay = day;
                options.backtestDays = days;
                options.silent = true;
                return options;
            }

            std::vector<TradeRecord> trades(const std::string& day) const
            {
                return TradeStore(dataRoot + "backtest/" + day).loadTrades();
            }
    };
}

TEST_F(LiveTraderBacktestTests, RunsTheChosenDaysFromSavedBars)
{
    LiveTrader trader(config(), backtest(FIRST_DAY, 2), dataRoot);
    ASSERT_EQ(trader.run(), 0);

    const LiveTrader::Result& result = trader.getResult();
    EXPECT_EQ(result.firstDay, FIRST_DAY);
    EXPECT_EQ(result.lastDay, SECOND_DAY);
    EXPECT_EQ(result.days, 2);
    EXPECT_DOUBLE_EQ(result.startingEquity, 100000.0);
    EXPECT_TRUE(fs::exists(dataRoot + "backtest/" + FIRST_DAY + "/trades.csv"));
}

TEST_F(LiveTraderBacktestTests, BuysTheDipAndTheStopLossSellsIt)
{
    LiveTrader trader(config(), backtest(FIRST_DAY), dataRoot);
    ASSERT_EQ(trader.run(), 0);

    // KO: bought on the drop, sold by its stop loss. PEP never traded.
    std::vector<TradeRecord> records = trades(FIRST_DAY);
    std::vector<TradeRecord> ko, pep;
    for (const auto& record : records)
        if (record.changedPosition())
            (record.ticker == "KO" ? ko : pep).push_back(record);
    ASSERT_EQ(ko.size(), 2u);
    EXPECT_EQ(ko[0].side, "buy");
    EXPECT_EQ(ko[0].time, FIRST_DAY + " " + clock(DROP_MINUTE));
    EXPECT_DOUBLE_EQ(ko[0].quantity, SHARES);
    EXPECT_NEAR(ko[0].stopLoss, DROP_TO * (1 - STOP_LOSS_PERCENT / 100), 0.05);
    EXPECT_EQ(ko[1].side, "sell");
    EXPECT_GT(ko[1].time, ko[0].time);
    EXPECT_TRUE(pep.empty());

    // Lost a little over 1% of 10 shares at $98 (about $10), plus slippage and fees; flat at the end
    const LiveTrader::Result& result = trader.getResult();
    double loss = result.startingEquity - result.endingEquity;
    EXPECT_GT(loss, SHARES * DROP_TO * STOP_LOSS_PERCENT / 100);
    EXPECT_LT(loss, SHARES * DROP_TO * 2 * STOP_LOSS_PERCENT / 100);
    EXPECT_EQ(result.fills, 2u);
    for (const auto& position : TradeStore(dataRoot + "backtest/" + FIRST_DAY).loadPositions())
        EXPECT_FLOAT_EQ(position.getQuantity(), 0);
    EXPECT_GT(result.mostCapitalInUse, 0);
}

TEST_F(LiveTraderBacktestTests, CapitalOptionSetsTheStartingCapital)
{
    LiveOptions options = backtest(FIRST_DAY);
    options.backtestCapital = 25000;
    LiveTrader trader(config(), options, dataRoot);
    ASSERT_EQ(trader.run(), 0);
    EXPECT_DOUBLE_EQ(trader.getResult().startingEquity, 25000);
    EXPECT_LT(trader.getResult().endingEquity, 25000);   // the same losing trade
}

TEST_F(LiveTraderBacktestTests, EveryRunStartsFreshAndGivesTheSameResult)
{
    LiveTrader first(config(), backtest(FIRST_DAY), dataRoot);
    ASSERT_EQ(first.run(), 0);
    LiveTrader second(config(), backtest(FIRST_DAY), dataRoot);
    ASSERT_EQ(second.run(), 0);

    EXPECT_DOUBLE_EQ(second.getResult().endingEquity, first.getResult().endingEquity);
    EXPECT_EQ(second.getResult().fills, first.getResult().fills);
    size_t changed = 0;
    for (const auto& record : trades(FIRST_DAY))
        changed += record.changedPosition();
    EXPECT_EQ(changed, 2u);   // not 4: the first run's records were cleared
}

TEST_F(LiveTraderBacktestTests, RegulatoryFeesCostMoney)
{
    json noFees = config();
    noFees["regulatory_fees"] = {{"sec_fee_per_million", 0}, {"finra_taf_per_share", 0}, {"finra_taf_max", 0}};
    LiveTrader withFees(config(), backtest(FIRST_DAY), dataRoot);
    ASSERT_EQ(withFees.run(), 0);
    LiveTrader without(noFees, backtest(FIRST_DAY), dataRoot);
    ASSERT_EQ(without.run(), 0);

    // One sell of 10 shares at about $97: SEC 970 x 20.60 / 1,000,000 + FINRA 10 x 0.000166, about 2 cents
    double fees = without.getResult().endingEquity - withFees.getResult().endingEquity;
    EXPECT_NEAR(fees, 0.02, 0.005);
}

TEST_F(LiveTraderBacktestTests, NoSavedBarsForAStockIsAnError)
{
    json config = LiveTraderBacktestTests::config();
    config["portfolio"].push_back({{"ticker", "AAPL"}, {"strategy", config["portfolio"][0]["strategy"]}});
    LiveTrader trader(config, backtest(""), dataRoot);   // no day: no download, just the saved days
    EXPECT_EQ(trader.run(), 1);
}

TEST_F(LiveTraderBacktestTests, NoPortfolioIsAnError)
{
    json config = LiveTraderBacktestTests::config();
    config.erase("portfolio");
    LiveTrader trader(config, backtest(FIRST_DAY), dataRoot);
    EXPECT_EQ(trader.run(), 1);
}

// ---------- What the dashboard reads ----------

TEST_F(LiveTraderBacktestTests, WritesTheDashboardsStatusAndEquityCurve)
{
    LiveOptions options = backtest(FIRST_DAY);
    options.silent = false;   // quiet: only the recap printed, and status.json written
    options.quiet = true;
    LiveTrader trader(config(), options, dataRoot);
    ASSERT_EQ(trader.run(), 0);

    std::string records = dataRoot + "backtest/" + FIRST_DAY + "/";
    std::ifstream statusFile(records + "status.json");
    json status = json::parse(statusFile);
    EXPECT_EQ(status["state"], "stopped");
    EXPECT_GT(status["pid"].get<long>(), 0);
    EXPECT_TRUE(status["run"]["backtest"].get<bool>());
    EXPECT_EQ(status["run"]["stocks"].size(), 2u);
    EXPECT_EQ(status["last_bar_time"], FIRST_DAY + " 15:59:00");
    ASSERT_EQ(status["stocks"].size(), 2u);
    EXPECT_EQ(status["stocks"][0]["ticker"], "KO");
    EXPECT_FALSE(status["stocks"][0]["strategies"].empty());
    EXPECT_TRUE(status["account"]["available"].get<bool>());
    EXPECT_EQ(status["estimate"]["fills"], 2);
    bool sawTheOrder = false;
    for (const auto& event : status["events"])
        sawTheOrder |= event["ticker"] == "KO";
    EXPECT_TRUE(sawTheOrder);

    // One line a minute
    std::ifstream equityFile(records + "equity.csv");
    std::string header, line;
    std::getline(equityFile, header);
    EXPECT_EQ(header, "time,equity");
    int lines = 0;
    while (std::getline(equityFile, line))
        lines++;
    EXPECT_EQ(lines, MINUTES_PER_DAY);
}

TEST_F(LiveTraderBacktestTests, NothingForTheDashboardWhenSilent)
{
    LiveOptions options = backtest(FIRST_DAY);
    options.silent = true;   // as risk_app runs many backtests
    LiveTrader trader(config(), options, dataRoot);
    ASSERT_EQ(trader.run(), 0);
    EXPECT_FALSE(fs::exists(dataRoot + "backtest/" + FIRST_DAY + "/status.json"));
}

TEST_F(LiveTraderBacktestTests, FlattenClosesEveryPositionThenStops)
{
    // A stop too wide to trigger, so KO is still held when the kill switch is pulled. 60 minutes a second,
    // so the day takes long enough to pull it.
    json wideStop = config();
    for (auto& stock : wideStop["portfolio"])
        stock["strategy"]["stop_loss"] = 30;
    LiveOptions options = backtest(FIRST_DAY);
    options.backtestSpeed = 60;
    options.silent = false;
    options.quiet = true;
    std::string statusPath = dataRoot + "backtest/" + FIRST_DAY + "/status.json";

    std::thread killSwitch([&] {
        // Wait until status.json shows KO held, then flatten
        for (int i = 0; i < 400; i++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            std::ifstream file(statusPath);
            json status = json::parse(file, nullptr, false);
            if (status.is_discarded() || !status.contains("stocks"))
                continue;
            for (const auto& stock : status["stocks"])
                if (stock["ticker"] == "KO" && stock["position"]["quantity"].get<double>() > 0)
                {
                    LiveTrader::requestFlatten();
                    return;
                }
        }
    });
    LiveTrader trader(wideStop, options, dataRoot);
    int exitCode = trader.run();
    killSwitch.join();
    ASSERT_EQ(exitCode, 0);

    std::vector<TradeRecord> records = trades(FIRST_DAY);
    ASSERT_FALSE(records.empty());
    const TradeRecord& last = records.back();
    EXPECT_EQ(last.strategy, "flatten");
    EXPECT_EQ(last.side, "sell");
    EXPECT_DOUBLE_EQ(last.quantity, SHARES);
    for (const auto& position : TradeStore(dataRoot + "backtest/" + FIRST_DAY).loadPositions())
        EXPECT_FLOAT_EQ(position.getQuantity(), 0);

    // Stopped early, not at the end of the day
    std::ifstream file(statusPath);
    json status = json::parse(file);
    EXPECT_EQ(status["state"], "stopped");
    EXPECT_LT(status["last_bar_time"].get<std::string>(), FIRST_DAY + " 15:59:00");
}
