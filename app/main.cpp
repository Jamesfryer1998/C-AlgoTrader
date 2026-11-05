#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <thread>
#include "../src/alpaca/AlpacaBroker.hpp"
#include "../src/alpaca/AlpacaStream.hpp"
#include "../src/data_access/MarketData.hpp"
#include "../src/strategy_engine/StrategyEngine.hpp"
#include "../src/strategy_engine/StrategyFactory.hpp"
#include "../src/strategy_engine/Portfolio.hpp"
#include "../src/oms/PerformanceTracker.hpp"
#include "../src/oms/TradeStore.hpp"
#include "../src/util/DotEnv.hpp"

/*
 * Live trading, all in one process:
 *
 *   Alpaca WebSocket ──(1-minute bar for TXN)──> TXN's MarketData ──> TXN's strategies ─┐
 *                    ──(1-minute bar for WMT)──> WMT's MarketData ──> WMT's strategies ─┼─(signal)──> OMS ──> Alpaca REST order
 *                    ...                                                                ┘
 *
 * The stocks and their strategies come from "portfolio" in src/config/config.json (one stock with the
 * top-level "ticker" and "strategies" if there is no portfolio). Each stock has its own price history and
 * strategies; one OMS, one trade log and one P&L book cover them all.
 *
 * Nothing runs on a timer: each new bar triggers that stock's decision as soon as it arrives.
 *
 * Alpaca keys are read from .env in the project root (copy .env.example to .env and fill it in).
 *
 * Trades, positions and a P&L snapshot are saved to data/live/ (data/live_dry_run/ in dry runs)
 * and reloaded on the next start, so a restart carries on where it left off.
 */

// The start of each bar's summary line, e.g. "[2026-10-30 12:24:00 New York] NVDA 204.67 vol 12969"
static void printBar(const MarketCondition& bar)
{
    std::cout << "[" << bar.DateTime << " New York] " << bar.Ticker << " " << std::fixed << std::setprecision(2)
              << bar.Close << " vol " << bar.Volume << " | ";
}

// Dry runs only: Alpaca isn't holding the printed orders, so show our own estimate (fills at bar closes)
static void printEstimate(const PerformanceSummary& s)
{
    std::cout << std::fixed << std::setprecision(2)
              << "P&L (dry run estimate): realised $" << s.realisedPnL << " | unrealised $" << s.unrealisedPnL
              << " | total $" << s.totalPnL << " (" << s.roiPercent << "%)"
              << " | trades " << s.fills << std::endl;
}

// e.g. +$12.30 / -$50.67
static std::string money(double value)
{
    std::ostringstream out;
    out << (value < 0 ? "-$" : "+$") << std::fixed << std::setprecision(2) << std::fabs(value);
    return out.str();
}

static double field(const json& j, const char* key)
{
    try { return j.contains(key) && j[key].is_string() ? std::stod(j[key].get<std::string>()) : 0.0; }
    catch (const std::exception&) { return 0.0; }
}

// P&L straight from Alpaca (real fills, Alpaca's latest price). Returns it for saving.
static json printAlpaca(AlpacaBroker& broker, const std::string& ticker)
{
    AlpacaBroker::AccountSnapshot account = broker.getAccount();
    const json& position = broker.lastPositionDetails();
    json snapshot = json::object();
    std::cout << std::fixed << std::setprecision(2);

    if (account.ok)
    {
        double today = account.equity - account.lastEquity;
        std::cout << "equity $" << account.equity;
        snapshot["equity"] = account.equity;
        snapshot["today_pnl"] = std::round(today * 100) / 100;
        snapshot["cash"] = account.cash;
    }
    else
    {
        std::cout << "account unavailable";
    }

    if (!position.empty())
    {
        double unrealised = field(position, "unrealized_pl");
        std::cout << " | holding " << position.value("qty", "0") << " @ $" << field(position, "avg_entry_price")
                  << " | unrealised " << money(unrealised)
                  << " (" << field(position, "unrealized_plpc") * 100.0 << "%)";
        snapshot["position"] = {{"ticker", ticker}, {"qty", field(position, "qty")},
                                {"avg_entry_price", field(position, "avg_entry_price")},
                                {"current_price", field(position, "current_price")},
                                {"market_value", field(position, "market_value")},
                                {"unrealized_pl", unrealised},
                                {"unrealized_intraday_pl", field(position, "unrealized_intraday_pl")}};
    }
    else
    {
        std::cout << " | no position";
    }
    std::cout << std::endl;
    return snapshot;
}

// While alive, everything written to std::cout goes into a buffer instead, so a bar's decision
// output can be shown under its summary line (which needs the post-decision P&L). Restores on exit.
struct CaptureStdout
{
    std::ostringstream buffer;
    std::streambuf* original;
    CaptureStdout() : original(std::cout.rdbuf(buffer.rdbuf())) {}
    ~CaptureStdout() { std::cout.rdbuf(original); }
    std::string release() { std::cout.rdbuf(original); return buffer.str(); }
};

static std::atomic<bool> running{true};

static void onStopSignal(int)
{
    running = false;
}

// One traded stock: its own price history and strategies (the OMS is shared)
struct Lane
{
    std::string ticker;
    json config;
    MarketData marketData;
    std::unique_ptr<StrategyFactory> factory;
    StrategyEngine engine;
    size_t maxBars = 1;
};

int main()
{
    std::signal(SIGINT, onStopSignal);   // Ctrl+C
    std::signal(SIGTERM, onStopSignal);  // systemctl stop / kill

    Config config;
    DotEnv::load(config.getProjectRoot() + "/.env"); // API keys, kept out of git
    json algoConfig = config.loadConfig();

    // 1. Broker: check our keys work and the account is reachable
    AlpacaBroker broker(algoConfig);
    if (!broker.connect())
        return 1;

    // 2. What to trade: one lane per stock, each with its own strategies
    std::vector<PortfolioEntry> portfolio;
    try
    {
        portfolio = Portfolio::fromConfig(algoConfig);
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR - " << e.what() << " (src/config/config.json)" << std::endl;
        return 1;
    }

    AlpacaStream stream(algoConfig);
    if (stream.getFeed() == "test" && portfolio.size() > 1)
    {
        std::cout << "Test feed: only FAKEPACA streams, so trading it alone with " << portfolio.front().ticker
                  << "'s strategies" << std::endl;
        portfolio.resize(1);
    }
    if (stream.getFeed() == "test")
        portfolio.front().ticker = stream.getSymbol();

    OrderManagement oms;   // shared by every lane
    oms.setUp(algoConfig, &broker);

    std::vector<std::unique_ptr<Lane>> lanes;
    std::map<std::string, Lane*> laneByTicker;
    std::vector<std::string> tickers;
    for (const auto& entry : portfolio)
    {
        auto lane = std::make_unique<Lane>();
        lane->ticker = entry.ticker;
        lane->config = entry.config;
        lane->factory = std::make_unique<StrategyFactory>(lane->config);
        lane->engine.useSharedOms(&oms);
        lane->engine.setUp(lane->config, *lane->factory, lane->marketData, &broker);
        laneByTicker[lane->ticker] = lane.get();
        tickers.push_back(lane->ticker);
        lanes.push_back(std::move(lane));
    }
    stream.setSymbols(tickers);

    // 3. Market data: back-fill exactly the bars each stock's strategies need (e.g. MEANREV lookback 120
    //    needs 120, MACD 16/50/5 needs 275) so they decide from the first live bar, and keep that many in memory
    std::cout << "Trading " << lanes.size() << " stock(s):" << std::endl;
    for (auto& lane : lanes)
    {
        std::string names;
        for (const auto& s : lane->config["strategies"])
            if (s.value("active", 0))
                names += (names.empty() ? "" : ", ") + s.value("name", std::string("?"));
        std::cout << "  " << lane->ticker << ": " << names << std::endl;

        int needed = lane->engine.barsNeeded();
        lane->maxBars = static_cast<size_t>(std::max(needed, 1));
        std::vector<MarketCondition> history = stream.fetchRecentBars(lane->ticker, needed);
        lane->marketData.update(history);
    }

    // 4. Saved state: reload trades/positions from the last run, then check each stock against what Alpaca holds
    std::string stateDir = config.getProjectRoot() + "/" + algoConfig.value("marketDataBasePath", "data/")
                         + (broker.isDryRun() ? "live_dry_run" : "live");
    TradeStore store(stateDir);
    PerformanceTracker tracker(algoConfig.value("starting_capital", 100000.0));
    oms.setMarketData(lanes.front()->marketData);
    oms.enablePersistence(&store, &tracker);
    for (auto& lane : lanes)
    {
        oms.setMarketData(lane->marketData);
        oms.syncPosition(lane->ticker);
    }

    // 5. On every new bar: store it in its stock's lane, run that stock's strategies (which may send an
    //    order), report the timing. Bars arrive one at a time on the stream's thread.
    std::string lastEquityMinute;
    json alpacaSnapshots = json::object();
    stream.start([&](const MarketCondition& bar)
    {
        auto found = laneByTicker.find(bar.Ticker);
        if (found == laneByTicker.end())
            return;   // not one of ours
        Lane& lane = *found->second;

        // Hold back the strategies' output so the bar's summary line can be printed first
        CaptureStdout decisionOutput;
        auto started = std::chrono::steady_clock::now();

        lane.marketData.addBar(bar, lane.maxBars);
        lane.engine.run();   // points the shared OMS at this stock's prices, then decides

        auto took = std::chrono::steady_clock::now() - started;
        std::cout << "Decision took " << std::fixed << std::setprecision(2)
                  << std::chrono::duration<double, std::milli>(took).count() << "ms" << std::endl;

        // After the decision (not timed): catch up with fills Alpaca made itself (stop loss / take profit),
        // update our records and show P&L
        oms.setMarketData(lane.marketData);
        oms.syncPosition(bar.Ticker);
        tracker.markPrice(bar.Ticker, bar.Close);
        if (bar.DateTime != lastEquityMinute)
        {
            tracker.recordEquity();   // once per minute, however many stocks report in it
            lastEquityMinute = bar.DateTime;
        }
        PerformanceSummary summary = tracker.summary();

        // One line per bar: the bar, then Alpaca's real P&L for that stock (our estimate in dry runs).
        // Saved: both, in performance.json
        json performance = {{"estimate", PerformanceTracker::toJson(summary)}};
        std::string decisionLines = decisionOutput.release();
        std::cout << "\n";   // blank line between bars
        printBar(bar);
        if (broker.isDryRun())
            printEstimate(summary);
        else
        {
            alpacaSnapshots[bar.Ticker] = printAlpaca(broker, bar.Ticker);
            performance["alpaca"] = alpacaSnapshots;
        }
        std::cout << decisionLines << std::flush;
        store.savePerformance(performance);
    });

    // 6. Bars arrive on the stream's own thread; this thread just waits for Ctrl+C
    while (running)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "\nStopping..." << std::endl;
    stream.stop();
    if (broker.isDryRun())
        printEstimate(tracker.summary());
    else
    {
        for (const auto& lane : lanes)
        {
            broker.getLatestPosition(lane->ticker);   // refresh Alpaca's position for the final line
            std::cout << lane->ticker << ": ";
            printAlpaca(broker, lane->ticker);
        }
    }
    std::cout << "State saved in " << stateDir << std::endl;
    return 0;
}
