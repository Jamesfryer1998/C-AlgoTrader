#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BrokerSession.hpp"
#include "CapitalUsage.hpp"
#include "EquityStats.hpp"
#include "LiveOptions.hpp"
#include "MinuteReport.hpp"
#include "StatusWriter.hpp"
#include "StockLane.hpp"
#include "../data_access/BarFeed.hpp"
#include "../oms/OrderManagement.hpp"
#include "../oms/PerformanceTracker.hpp"
#include "../oms/TradeStore.hpp"
#include "../risk/RiskEngine.hpp"
#include "../strategy_engine/Portfolio.hpp"

using json = nlohmann::json;

/**
 * LiveTrader
 *
 * One trading session, all in one process. How it runs:
 *
 *   main()
 *    └─ LiveTrader::run()
 *        ├─ loadPortfolio, createLanes     the stocks in config/strategy.json, each with its own strategies
 *        ├─ openFeed, connectBroker        live: Alpaca's stream and Alpaca; --backtest: saved bars, simulated broker
 *        ├─ backfillLanes, openRecords     history so strategies can decide at once; trades.csv etc.
 *        └─ feed.start ──> onBar(bar), once per stock per minute, on the feed's thread:
 *              1. lane.addBar(bar)                      the new price
 *              2. lane.decide(held)                     every strategy proposes BUY / SELL / nothing;
 *                                                       they must agree (StrategyEngine)
 *              3. lane.size(order, ...)                 how many shares (the RiskEngine in config "risk")
 *              4. sendOrder(lane, order)                risk checks, then the broker (OrderManagement)
 *              5. updateBook(lane, bar)                 positions and P&L catch up with the broker
 *              6. addToReport(...)                      one row in this minute's table (MinuteReport)
 *
 * The stocks and their strategies come from "portfolio" in config/strategy.json (see Portfolio). One OMS, one trade log
 * and one P&L book cover every stock. Nothing runs on a timer: each bar is decided as soon as it arrives.
 *
 * Backtest (--backtest) is the same session with a BacktestStream feed and a BacktestSession broker, so the whole
 * process can be watched without the market open or orders reaching Alpaca. Records go to data/backtest/<day>/.
 */
class LiveTrader
{
    public:
        LiveTrader(const json& config, const LiveOptions& options, const std::string& dataRoot);

        // Runs until Ctrl+C (or the end of a backtest). Returns the process exit code.
        int run();

        // For the SIGINT / SIGTERM handler
        static void requestStop() { stopRequested = true; }

        // For the SIGUSR1 handler (the dashboard's kill switch): close every position, then stop
        static void requestFlatten() { flattenRequested = true; }

        // How a session went, once run() has returned (e.g. to compare risk engines)
        struct Result
        {
            double startingEquity = 0;
            double endingEquity = 0;
            double maxDrawdown = 0;       // fraction
            double sharpe = 0;            // of daily returns, annualised
            double mostCapitalInUse = 0;
            size_t fills = 0;
            int days = 0;
            std::string firstDay, lastDay;   // a backtest's first and last day
        };
        const Result& getResult() const { return result; }

    private:
        // Setting up
        bool loadPortfolio();
        void createLanes();
        bool openFeed();
        bool connectBroker();
        void backfillLanes();
        void openRecords();
        void printStartup();
        std::string describeRecords() const;

        // Running
        void startFeed();
        void waitUntilFinished();
        void stopFeed();
        void printResultsHeading() const;

        // Each bar, on the feed's thread
        void onBar(const MarketCondition& bar);
        void sendOrder(const StockLane& lane, Order& order);
        void updateBook(const StockLane& lane, const MarketCondition& bar);
        void addToReport(const StockLane& lane, const MarketCondition& bar, double decisionMs,
                         const std::string& notes);
        void savePerformance(bool now);

        // The dashboard's status.json and equity.csv (StatusWriter); not written when silent
        bool writesStatus() const { return !options.silent && store != nullptr; }
        void describeRunForStatus();
        void saveStatus(bool now);
        void keepStatusFresh();

        // Kill switch: send an order closing each position, on the main thread
        void flattenAll();
        RiskEngine::SizingContext sizingContext(const std::string& ticker, float held);
        void collectResult();

        // Where messages go: the terminal, or nowhere with --quiet (or silent)
        std::ostream& screen() const;

        // Where the end-of-session recap goes: the terminal unless silent
        std::ostream& recap() const;

        static json withOptions(json config, const LiveOptions& options);

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
        CapitalUsage capital;       // the most money tied up in positions at once, for the recap
        EquityStats equityStats;    // drawdown and Sharpe, for the result
        double startingEquity = 0;
        Result result;
        std::string lastEquityMinute;
        json savedPositions = json::object();
        AccountSnapshot lastAccount;
        std::chrono::steady_clock::time_point lastPerformanceSave;

        // Output (the feed's thread adds rows; the main thread prints overdue tables)
        std::mutex reportLock;
        std::unique_ptr<MinuteReport> report;

        // What the dashboard reads
        StatusWriter status;
        std::chrono::steady_clock::time_point lastClockCheck;

        // One decision at a time: a bar on the feed's thread, or flattening on the main one
        std::mutex tradingLock;

        static std::atomic<bool> stopRequested;
        static std::atomic<bool> flattenRequested;
};
