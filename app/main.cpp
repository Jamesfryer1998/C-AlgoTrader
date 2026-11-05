#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
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
#include "../src/util/TextTable.hpp"

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

// e.g. +$12.30 / -$50.67
static std::string money(double value)
{
    std::ostringstream out;
    out << (value < 0 ? "-$" : "+$") << std::fixed << std::setprecision(2) << std::fabs(value);
    return out.str();
}

// e.g. $99,674.35
static std::string dollars(double value)
{
    std::ostringstream digits;
    digits << std::fixed << std::setprecision(2) << std::fabs(value);
    std::string text = digits.str();
    for (int i = static_cast<int>(text.find('.')) - 3; i > 0; i -= 3)
        text.insert(static_cast<size_t>(i), ",");
    return (value < 0 ? "-$" : "$") + text;
}

static std::string fixed2(double value)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

static double field(const json& j, const char* key)
{
    try { return j.contains(key) && j[key].is_string() ? std::stod(j[key].get<std::string>()) : 0.0; }
    catch (const std::exception&) { return 0.0; }
}

// One stock's line(s) in the minute's table
struct MinuteRow
{
    bool seen = false;
    double price = 0;
    std::vector<StrategyBase::Levels> levels;
    std::string position = "-";
    std::string unrealised = "-";
    double decisionMs = 0;
};

// Everything shown for one minute: a row per stock, the account, and what happened (orders etc.)
struct MinuteView
{
    std::string minute;   // the bars' time, e.g. "2026-11-05 15:57:00"
    bool open = false;
    std::chrono::steady_clock::time_point firstBar;
    std::map<std::string, MinuteRow> rows;
    std::vector<std::string> events;
    AlpacaBroker::AccountSnapshot account;
    std::string estimate;   // dry runs: our own P&L estimate
};

// Alpaca's position for one stock as table cells ("1 @ 104.65", "+$0.32 (+0.31%)"), plus a snapshot to save
static json alpacaPosition(AlpacaBroker& broker, const std::string& ticker, MinuteRow& row)
{
    broker.getLatestPosition(ticker);   // fresh: the OMS skips its check while an order is filling
    const json& position = broker.lastPositionDetails();
    if (position.empty())
        return json::object();

    double qty = field(position, "qty"), unrealised = field(position, "unrealized_pl");
    if (position.value("side", "long") == "short")
        qty = -std::fabs(qty);
    std::ostringstream cell;
    cell << qty << " @ " << fixed2(field(position, "avg_entry_price"));
    row.position = cell.str();
    row.unrealised = money(unrealised) + " (" + (unrealised >= 0 ? "+" : "") +
                     fixed2(field(position, "unrealized_plpc") * 100.0) + "%)";
    return {{"ticker", ticker}, {"qty", qty}, {"avg_entry_price", field(position, "avg_entry_price")},
            {"current_price", field(position, "current_price")}, {"market_value", field(position, "market_value")},
            {"unrealized_pl", unrealised}, {"unrealized_intraday_pl", field(position, "unrealized_intraday_pl")}};
}

// Print the minute as one table, then clear it
static void printMinute(MinuteView& view, const std::vector<std::string>& tickers, const std::map<std::string, double>& lastPrice)
{
    if (!view.open)
        return;

    std::cout << "\n=== " << view.minute.substr(0, 16) << " New York";
    if (view.account.ok)
        std::cout << " | equity " << dollars(view.account.equity) << " | today "
                  << money(view.account.equity - view.account.lastEquity);
    if (!view.estimate.empty())
        std::cout << " | " << view.estimate;
    std::cout << " ===\n";

    TextTable table({"Stock", "Strategy", "Price", "Lower", "Current", "Upper", "State", "Position", "Unrealised", "Decision"},
                    {false, false, true, true, true, true, false, false, true, true});
    for (const auto& ticker : tickers)
    {
        const MinuteRow& row = view.rows[ticker];
        if (!row.seen)
        {
            auto last = lastPrice.find(ticker);
            table.addRow({ticker, "", last != lastPrice.end() ? fixed2(last->second) : "-", "", "", "",
                          "no bar this minute", "", "", ""});
            continue;
        }
        for (size_t i = 0; i < row.levels.size(); i++)
        {
            const auto& l = row.levels[i];
            bool first = i == 0;
            table.addRow({first ? ticker : "", l.label, first ? fixed2(row.price) : "", l.lower.empty() ? "-" : l.lower,
                          l.current.empty() ? "-" : l.current, l.upper.empty() ? "-" : l.upper, l.state,
                          first ? row.position : "", first ? row.unrealised : "",
                          first ? fixed2(row.decisionMs) + " ms" : ""});
        }
    }
    std::cout << table.render();
    for (const auto& event : view.events)
        std::cout << "  " << event << "\n";
    std::cout << std::flush;

    view = MinuteView();
}

// While alive, everything written to std::cout and std::cerr goes into one buffer instead, so what a bar's
// decision printed (orders sent, rejections) can be listed under the minute's table. Restores on exit.
struct CaptureStdout
{
    std::ostringstream buffer;
    std::streambuf* originalOut;
    std::streambuf* originalErr;
    CaptureStdout() : originalOut(std::cout.rdbuf(buffer.rdbuf())), originalErr(std::cerr.rdbuf(buffer.rdbuf())) {}
    ~CaptureStdout() { restore(); }
    void restore() { std::cout.rdbuf(originalOut); std::cerr.rdbuf(originalErr); }
    std::string release() { restore(); return buffer.str(); }
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
    //    order) and add the stock's row to this minute's table. Bars arrive one at a time on the stream's
    //    thread; the table is printed once every stock has reported for the minute (or after a few seconds,
    //    since a quiet stock may have no IEX trades, and so no bar, in a minute).
    for (auto& lane : lanes)
        lane->engine.setPrintStatus(false);   // the table shows each strategy's levels instead
    std::cout << "\nEach minute: one row per stock. Lower / Upper are the levels that make a strategy trade (buy at or\n"
              << "below Lower, sell at or above Upper); Current is what's compared with them: the price, or the RSI or\n"
              << "MACD value. \"exit\" marks where an open trade will be closed. Orders and errors are listed underneath.\n"
              << std::flush;
    std::string lastEquityMinute;
    json alpacaSnapshots = json::object();
    std::mutex viewLock;
    MinuteView view;
    std::map<std::string, double> lastPrice;
    const auto waitForStragglers = std::chrono::seconds(8);

    stream.start([&](const MarketCondition& bar)
    {
        auto found = laneByTicker.find(bar.Ticker);
        if (found == laneByTicker.end())
            return;   // not one of ours
        Lane& lane = *found->second;

        // Hold back anything the decision prints (orders sent, strategy notes): listed under the table
        CaptureStdout decisionOutput;
        auto started = std::chrono::steady_clock::now();

        lane.marketData.addBar(bar, lane.maxBars);
        lane.engine.run();   // points the shared OMS at this stock's prices, then decides

        double decisionMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        std::string decisionLines = decisionOutput.release();

        // After the decision (not timed): catch up with fills Alpaca made itself (stop loss / take profit),
        // update our records and fetch P&L
        oms.setMarketData(lane.marketData);
        oms.syncPosition(bar.Ticker);
        tracker.markPrice(bar.Ticker, bar.Close);
        if (bar.DateTime != lastEquityMinute)
        {
            tracker.recordEquity();   // once per minute, however many stocks report in it
            lastEquityMinute = bar.DateTime;
        }
        PerformanceSummary summary = tracker.summary();

        MinuteRow row;
        row.seen = true;
        row.price = bar.Close;
        row.levels = lane.engine.levels();
        row.decisionMs = decisionMs;
        json performance = {{"estimate", PerformanceTracker::toJson(summary)}};
        AlpacaBroker::AccountSnapshot account;
        if (broker.isDryRun())
        {
            for (const auto& position : oms.getPositions())
                if (position.getTicker() == bar.Ticker && position.getQuantity() != 0)
                {
                    std::ostringstream cell;
                    cell << position.getQuantity() << " @ " << fixed2(position.getAvgPrice());
                    row.position = cell.str();
                    row.unrealised = money((bar.Close - position.getAvgPrice()) * position.getQuantity());
                }
        }
        else
        {
            account = broker.getAccount();
            alpacaSnapshots[bar.Ticker] = alpacaPosition(broker, bar.Ticker, row);
            performance["alpaca"] = {{"equity", account.equity}, {"today_pnl", account.equity - account.lastEquity},
                                     {"cash", account.cash}, {"positions", alpacaSnapshots}};
        }
        store.savePerformance(performance);

        std::lock_guard<std::mutex> guard(viewLock);
        if (view.open && view.minute != bar.DateTime)
            printMinute(view, tickers, lastPrice);   // a new minute started before every stock reported
        if (!view.open)
        {
            view.open = true;
            view.minute = bar.DateTime;
            view.firstBar = std::chrono::steady_clock::now();
        }
        view.rows[bar.Ticker] = row;
        lastPrice[bar.Ticker] = bar.Close;
        if (account.ok)
            view.account = account;
        if (broker.isDryRun())
            view.estimate = "dry run estimate " + money(summary.totalPnL);
        std::istringstream lines(decisionLines);
        for (std::string line; std::getline(lines, line);)
            if (!line.empty())
                view.events.push_back(bar.Ticker + ": " + line);

        bool everyStock = true;
        for (const auto& ticker : tickers)
            everyStock = everyStock && view.rows[ticker].seen;
        if (everyStock)
            printMinute(view, tickers, lastPrice);
    });

    // 6. Bars arrive on the stream's own thread; this thread waits for Ctrl+C, and prints a minute's table
    //    if some stock still hasn't reported a few seconds after the first one did
    while (running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::lock_guard<std::mutex> guard(viewLock);
        if (view.open && std::chrono::steady_clock::now() - view.firstBar > waitForStragglers)
            printMinute(view, tickers, lastPrice);
    }

    std::cout << "\nStopping..." << std::endl;
    stream.stop();
    {
        std::lock_guard<std::mutex> guard(viewLock);
        printMinute(view, tickers, lastPrice);
    }
    if (broker.isDryRun())
        std::cout << "Dry run estimate: " << money(tracker.summary().totalPnL) << std::endl;
    else
    {
        AlpacaBroker::AccountSnapshot account = broker.getAccount();
        if (account.ok)
            std::cout << "Equity " << dollars(account.equity) << " (today "
                      << money(account.equity - account.lastEquity) << ")" << std::endl;
        for (const auto& lane : lanes)
        {
            MinuteRow row;
            alpacaPosition(broker, lane->ticker, row);
            std::cout << "  " << lane->ticker << ": " << (row.position == "-" ? "no position"
                         : row.position + ", unrealised " + row.unrealised) << std::endl;
        }
    }
    std::cout << "State saved in " << stateDir << std::endl;
    return 0;
}
