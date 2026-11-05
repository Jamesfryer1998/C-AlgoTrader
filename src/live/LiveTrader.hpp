#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BrokerSession.hpp"
#include "LiveOptions.hpp"
#include "MinuteReport.hpp"
#include "StockLane.hpp"
#include "../data_access/BarFeed.hpp"
#include "../oms/OrderManagement.hpp"
#include "../oms/PerformanceTracker.hpp"
#include "../oms/TradeStore.hpp"
#include "../strategy_engine/Portfolio.hpp"

using json = nlohmann::json;

/**
 * LiveTrader
 *
 * One trading session, all in one process. How it runs:
 *
 *   main()
 *    └─ LiveTrader::run()
 *        ├─ loadPortfolio, createLanes     the stocks in config.json, each with its own strategies
 *        ├─ openFeed, connectBroker        live: Alpaca's stream and Alpaca; --replay: saved bars, simulated broker
 *        ├─ backfillLanes, openRecords     history so strategies can decide at once; trades.csv etc.
 *        └─ feed.start ──> onBar(bar), once per stock per minute, on the feed's thread:
 *              1. lane.addBar(bar)                      the new price
 *              2. lane.decide(held)                     every strategy proposes BUY / SELL / nothing;
 *                                                       they must agree (StrategyEngine)
 *              3. sendOrder(lane, order)                risk checks, then the broker (OrderManagement)
 *              4. updateBook(lane, bar)                 positions and P&L catch up with the broker
 *              5. addToReport(...)                      one row in this minute's table (MinuteReport)
 *
 * The stocks and their strategies come from "portfolio" in config.json (see Portfolio). One OMS, one trade log
 * and one P&L book cover every stock. Nothing runs on a timer: each bar is decided as soon as it arrives.
 *
 * Replay (--replay) is the same session with a ReplayStream feed and a ReplaySession broker, so the whole
 * process can be watched without the market open or orders reaching Alpaca. Records go to data/replay/<day>/.
 */
class LiveTrader
{
    public:
        LiveTrader(const json& config, const LiveOptions& options, const std::string& dataRoot);

        // Runs until Ctrl+C (or the end of a replay). Returns the process exit code.
        int run();

        // For the SIGINT / SIGTERM handler
        static void requestStop() { stopRequested = true; }

    private:
        // Setting up
        bool loadPortfolio();
        void createLanes();
        bool openFeed();
        bool connectBroker();
        void backfillLanes();
        void openRecords();

        // Running
        void startFeed();
        void waitUntilFinished();
        void stopFeed();

        // Each bar, on the feed's thread
        void onBar(const MarketCondition& bar);
        void sendOrder(const StockLane& lane, Order& order);
        void updateBook(const StockLane& lane, const MarketCondition& bar);
        void addToReport(const StockLane& lane, const MarketCondition& bar, double decisionMs,
                         const std::string& notes);
        void savePerformance(const MarketCondition& bar, const PositionSnapshot& position, const AccountSnapshot& account);

        json config;
        LiveOptions options;
        std::string dataRoot;

        // What we trade
        std::vector<PortfolioEntry> portfolio;
        std::vector<std::unique_ptr<StockLane>> lanes;
        std::map<std::string, StockLane*> laneByTicker;
        std::vector<std::string> tickers;

        // Where bars come from and where orders go
        std::unique_ptr<BarFeed> feed;
        std::unique_ptr<BrokerSession> session;
        OrderManagement oms;
        std::atomic<bool> feedFinished{false};

        // Records
        std::string recordsDir;
        std::unique_ptr<TradeStore> store;
        PerformanceTracker tracker;
        std::string lastEquityMinute;
        json savedPositions = json::object();

        // Output (the feed's thread adds rows; the main thread prints overdue tables)
        std::mutex reportLock;
        std::unique_ptr<MinuteReport> report;

        static std::atomic<bool> stopRequested;
};
