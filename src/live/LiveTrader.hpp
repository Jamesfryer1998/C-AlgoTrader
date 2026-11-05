#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "AccountMonitor.hpp"
#include "LiveOptions.hpp"
#include "MinuteReport.hpp"
#include "StockLane.hpp"
#include "../alpaca/AlpacaBroker.hpp"
#include "../alpaca/AlpacaStream.hpp"
#include "../backtest/ReplayStream.hpp"
#include "../broker/SimulatedBroker.hpp"
#include "../oms/OrderManagement.hpp"
#include "../oms/PerformanceTracker.hpp"
#include "../oms/TradeStore.hpp"
#include "../strategy_engine/Portfolio.hpp"

using json = nlohmann::json;

/**
 * LiveTrader
 *
 * One live trading session, all in one process:
 *
 *   Alpaca WebSocket ──(1-minute bar for TXN)──> TXN's lane (prices + strategies) ─┐
 *                    ──(1-minute bar for WMT)──> WMT's lane (prices + strategies) ─┼─(signal)──> OMS ──> Alpaca order
 *                    ...                                                            ┘
 *
 * The stocks and their strategies come from "portfolio" in config.json (see Portfolio). Each stock has its own
 * lane; one OMS, one trade log and one P&L book cover them all. Nothing runs on a timer: each new bar triggers
 * that stock's decision as soon as it arrives, and every minute is printed as one table (MinuteReport).
 *
 * Replay (--replay): the same session on saved 1-minute bars, with a simulated broker filling the orders, so
 * the whole process can be watched without the market open or orders reaching Alpaca. Records go to
 * data/replay/<day>/ instead of data/live/.
 */
class LiveTrader
{
    public:
        // How long a minute's table waits for stocks that haven't sent a bar (a quiet stock can have no IEX
        // trades, and so no bar, in a minute)
        static constexpr std::chrono::seconds WAIT_FOR_MISSING_BARS{8};
        // How often the main thread checks for Ctrl+C and overdue tables
        static constexpr std::chrono::milliseconds CHECK_INTERVAL{200};
        // Replay fills: slippage against us on every fill (config "replay_slippage_percent" overrides)
        static constexpr double DEFAULT_REPLAY_SLIPPAGE_PERCENT = 0.01;
        // The simulated broker only needs the bar being processed
        static constexpr size_t SIMULATED_BROKER_BARS = 5;

        LiveTrader(const json& config, const LiveOptions& options, const std::string& dataRoot);

        // Runs until Ctrl+C (or the end of a replay). Returns the process exit code.
        int run();

        // For the SIGINT / SIGTERM handler
        static void requestStop() { stopRequested = true; }

    private:
        bool connectBroker();
        bool loadPortfolio();
        bool loadReplay();
        void createLanes();
        void backfillLanes();
        void openRecords();
        void startBars();
        void waitUntilFinished();
        void stopBars();
        void printFinalSummary();

        // Everything that happens when a stock's bar arrives (on the stream's thread)
        void onBar(const MarketCondition& bar);
        void recordEquityOncePerMinute(const MarketCondition& bar);

        json config;
        LiveOptions options;
        std::string dataRoot;

        // Brokers: Alpaca when trading, the simulated one in a replay
        AlpacaBroker alpaca;
        MarketData simulatedFeed;
        SimulatedBroker simulated;
        BrokerBase* broker = nullptr;
        std::unique_ptr<AccountMonitor> monitor;
        SimulatedAccountMonitor* replayMonitor = nullptr;   // the monitor above, in a replay

        // What we trade
        std::vector<PortfolioEntry> portfolio;
        OrderManagement oms;
        std::vector<std::unique_ptr<StockLane>> lanes;
        std::map<std::string, StockLane*> laneByTicker;
        std::vector<std::string> tickers;

        // Where bars come from
        AlpacaStream stream;
        std::unique_ptr<ReplayStream> replay;
        std::atomic<bool> replayFinished{false};

        // Records
        std::string recordsDir;
        std::unique_ptr<TradeStore> store;
        PerformanceTracker tracker;
        std::string lastEquityMinute;
        json savedPositions = json::object();

        // Output (the stream's thread adds rows; the main thread prints overdue tables)
        std::mutex reportLock;
        std::unique_ptr<MinuteReport> report;

        static std::atomic<bool> stopRequested;
};
