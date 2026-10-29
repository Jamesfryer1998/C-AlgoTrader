#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <thread>
#include "../src/alpaca/AlpacaBroker.hpp"
#include "../src/alpaca/AlpacaStream.hpp"
#include "../src/data_access/MarketData.hpp"
#include "../src/strategy_engine/StrategyEngine.hpp"
#include "../src/strategy_engine/StrategyFactory.hpp"
#include "../src/oms/PerformanceTracker.hpp"
#include "../src/oms/TradeStore.hpp"
#include "../src/util/DotEnv.hpp"

/*
 * Live trading, all in one process:
 *
 *   Alpaca WebSocket ──(1-minute bar)──> MarketData ──> strategies (RSI/MACD/MEANREV)
 *                                                          │
 *                                                          └─(signal)──> OMS ──> Alpaca REST order
 *
 * Nothing runs on a timer: each new bar triggers one decision as soon as it arrives.
 * No CSV files and no desktop app needed.
 *
 * Alpaca keys are read from .env in the project root (copy .env.example to .env and fill it in).
 *
 * Trades, positions and a P&L snapshot are saved to data/live/ (data/live_dry_run/ in dry runs)
 * and reloaded on the next start, so a restart carries on where it left off.
 */

static void printPerformance(const PerformanceSummary& s)
{
    std::cout << std::fixed << std::setprecision(2)
              << "P&L: realised $" << s.realisedPnL << " | unrealised $" << s.unrealisedPnL
              << " | total $" << s.totalPnL << " (" << s.roiPercent << "%)"
              << " | trades " << s.fills << std::endl;
}

static std::atomic<bool> running{true};

static void onStopSignal(int)
{
    running = false;
}

int main()
{
    std::signal(SIGINT, onStopSignal);   // Ctrl+C
    std::signal(SIGTERM, onStopSignal);  // systemctl stop / kill

    Config config;
    DotEnv::load(config.getProjectRoot() + "/.env"); // API keys, kept out of git
    json algoConfig = config.loadConfig();
    json alpacaConfig = algoConfig.value("alpaca", json::object());
    int warmupBars = alpacaConfig.value("warmup_bars", 100);
    size_t maxBars = alpacaConfig.value("max_bars", 500);

    // 1. Broker: check our keys work and the account is reachable
    AlpacaBroker broker(algoConfig);
    if (!broker.connect())
        return 1;

    // 2. Market data: preload recent bars so strategies (e.g. RSI period 30) can decide from the first live bar
    AlpacaStream stream(algoConfig);
    MarketData marketData;
    std::vector<MarketCondition> history = stream.fetchRecentBars(warmupBars);
    marketData.update(history);

    // 3. Strategies + OMS
    StrategyFactory stratFactory(algoConfig);
    StrategyEngine stratEngine;
    stratEngine.setUp(algoConfig, stratFactory, marketData, &broker);

    // 4. Saved state: reload trades/positions from the last run, then check against what Alpaca holds
    std::string stateDir = config.getProjectRoot() + "/" + algoConfig.value("marketDataBasePath", "data/")
                         + (broker.isDryRun() ? "live_dry_run" : "live");
    TradeStore store(stateDir);
    PerformanceTracker tracker(algoConfig.value("starting_capital", 100000.0));
    OrderManagement* oms = stratEngine.getOms();
    oms->setMarketData(marketData);
    oms->enablePersistence(&store, &tracker);
    oms->syncPosition(stream.getSymbol());

    // 5. On every new bar: store it, run the strategies (which may send an order), report the timing
    stream.start([&](const MarketCondition& bar)
    {
        auto started = std::chrono::steady_clock::now();

        marketData.addBar(bar, maxBars);
        stratEngine.run();

        auto took = std::chrono::steady_clock::now() - started;
        std::cout << "Decision took " << std::chrono::duration<double, std::milli>(took).count() << "ms" << std::endl;

        // After the decision (not timed): catch up with fills Alpaca made itself (stop loss / take profit),
        // mark P&L at this bar's close and save a snapshot
        oms->syncPosition(bar.Ticker);
        tracker.markPrice(bar.Ticker, bar.Close);
        tracker.recordEquity();
        PerformanceSummary summary = tracker.summary();
        store.savePerformance(PerformanceTracker::toJson(summary));
        printPerformance(summary);
    });

    // 6. Bars arrive on the stream's own thread; this thread just waits for Ctrl+C
    while (running)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "\nStopping..." << std::endl;
    stream.stop();
    printPerformance(tracker.summary());
    std::cout << "State saved in " << stateDir << std::endl;
    return 0;
}
