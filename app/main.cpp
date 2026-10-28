#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#include "../src/alpaca/AlpacaBroker.hpp"
#include "../src/alpaca/AlpacaStream.hpp"
#include "../src/data_access/MarketData.hpp"
#include "../src/strategy_engine/StrategyEngine.hpp"
#include "../src/strategy_engine/StrategyFactory.hpp"

/*
 * Live trading, all in one process:
 *
 *   Alpaca WebSocket ──(1-minute bar)──> MarketData ──> strategies (RSI/MACD/MEANREV)
 *                                                          │
 *                                                          └─(signal)──> OMS ──> Alpaca REST order
 *
 * Nothing runs on a timer: each new bar triggers one decision as soon as it arrives.
 * No Python, no CSV files and no desktop app needed.
 *
 * Before running, set your Alpaca keys:
 *   export APCA_API_KEY_ID=...
 *   export APCA_API_SECRET_KEY=...
 */

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

    // 3. Strategies + OMS, exactly as before
    StrategyFactory stratFactory(algoConfig);
    StrategyEngine stratEngine;
    stratEngine.setUp(algoConfig, stratFactory, marketData, &broker);
    stratEngine.getOms()->syncPosition(stream.getSymbol()); // pick up anything we already hold

    // 4. On every new bar: store it, run the strategies (which may send an order), report the timing
    stream.start([&](const MarketCondition& bar)
    {
        auto started = std::chrono::steady_clock::now();

        marketData.addBar(bar, maxBars);
        stratEngine.run();

        auto took = std::chrono::steady_clock::now() - started;
        std::cout << "Decision took " << std::chrono::duration<double, std::milli>(took).count()
                  << " ms (includes the order round trip if an order was sent)" << std::endl;
    });

    // 5. Bars arrive on the stream's own thread; this thread just waits for Ctrl+C
    while (running)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "\nStopping..." << std::endl;
    stream.stop();
    return 0;
}
