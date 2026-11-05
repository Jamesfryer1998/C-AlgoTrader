#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <memory>
#include <sstream>
#include <thread>
#include "../src/alpaca/AlpacaBroker.hpp"
#include "../src/alpaca/AlpacaStream.hpp"
#include "../src/backtest/ReplayStream.hpp"
#include "../src/broker/SimulatedBroker.hpp"
#include "../src/data_access/MarketData.hpp"
#include "../src/strategy_engine/StrategyEngine.hpp"
#include "../src/strategy_engine/StrategyFactory.hpp"
#include "../src/strategy_engine/Portfolio.hpp"
#include "../src/oms/PerformanceTracker.hpp"
#include "../src/oms/TradeStore.hpp"
#include "../src/util/DotEnv.hpp"
#include "../src/util/TextTable.hpp"
#include "../src/util/Log.hpp"

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
 * Replay mode (--replay): the same process on saved 1-minute bars instead of Alpaca's stream, with a simulated
 * broker filling the orders, so it can be watched end to end without the market being open or real orders.
 * No Alpaca keys needed. Records go to data/replay/<day>/.
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

// The simulated broker's step-by-step notes (replay). Its fills ("Order executed") and stop loss / take profit
// hits are kept; this is the rest.
static bool simulatorBookkeeping(const std::string& line)
{
    for (const char* prefix : {"SimulatedBroker", "Order placed for order", "Established new", "Increased long",
                               "Increased short", "Closed long", "Covered short", "Completely closed", "Completely covered",
                               "Partially closed", "Partially covered", "Cash", "Total "})
        if (line.rfind(prefix, 0) == 0)
            return true;
    return false;
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

static void printUsage()
{
    std::cout << R"(Live trading on Alpaca (stocks and strategies from src/config/config.json)

Usage: algo_trader_app [options]
  (no options)              trade live on Alpaca's stream (paper or live account, from config.json)
  --replay [YYYY-MM-DD]     replay saved 1-minute bars with a simulated broker instead: no orders reach
                            Alpaca and no keys are needed (default day: the latest saved for every stock)
  --days 3                  replay this many trading days from that day (default 1)
  --speed 10                replayed minutes per second (default 10; 0 = as fast as possible)
)";
}

int main(int argc, char* argv[])
{
    std::signal(SIGINT, onStopSignal);   // Ctrl+C
    std::signal(SIGTERM, onStopSignal);  // systemctl stop / kill

    bool replay = false;
    std::string replayDay;
    int replayDays = 1;
    double replaySpeed = 10;
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        bool hasValue = i + 1 < argc && argv[i + 1][0] != '-';
        try
        {
            if (arg == "--help") { printUsage(); return 0; }
            else if (arg == "--replay") { replay = true; if (hasValue) replayDay = argv[++i]; }
            else if (arg == "--days" && hasValue) replayDays = std::max(1, std::stoi(argv[++i]));
            else if (arg == "--speed" && hasValue) replaySpeed = std::max(0.0, std::stod(argv[++i]));
            else { std::cerr << "Unknown argument: " << arg << "\n\n"; printUsage(); return 1; }
        }
        catch (const std::exception&)
        {
            std::cerr << "Bad value for " << arg << "\n";
            return 1;
        }
    }

    Config config;
    DotEnv::load(config.getProjectRoot() + "/.env"); // API keys, kept out of git
    json algoConfig = config.loadConfig();
    std::string dataRoot = config.getProjectRoot() + "/" + algoConfig.value("marketDataBasePath", "data/");

    // 1. Broker. Live: Alpaca (check our keys work and the account is reachable). Replay: a simulated broker
    //    that fills orders at each bar's close (plus slippage) and runs stop losses / take profits itself.
    AlpacaBroker alpaca(algoConfig);
    MarketData simulatedFeed;   // replay: the bar being processed, for the simulated broker's prices
    SimulatedBroker simulated(simulatedFeed);
    BrokerBase& broker = replay ? static_cast<BrokerBase&>(simulated) : static_cast<BrokerBase&>(alpaca);
    bool dryRun = !replay && alpaca.isDryRun();
    if (replay)
    {
        Log::verbose = false;   // the simulated broker's set-up notes
        simulated.followLatestBar(true);
        simulated.tradeSeveralStocks(true);
        simulated.setSlippageMode(SimulatedBroker::SlippageMode::Adverse);
        simulated.setSlippage(0.0001);   // 0.01% against us on every fill, as in the backtests
        simulated.setCommission(0.0);
        simulated.setStartingCapital(algoConfig.value("starting_capital", 100000.0));
        Log::verbose = true;
    }
    else if (!alpaca.connect())
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
    if (!replay && stream.getFeed() == "test" && portfolio.size() > 1)
    {
        std::cout << "Test feed: only FAKEPACA streams, so trading it alone with " << portfolio.front().ticker
                  << "'s strategies" << std::endl;
        portfolio.resize(1);
    }
    if (!replay && stream.getFeed() == "test")
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

    std::unique_ptr<ReplayStream> replayStream;
    if (replay)
    {
        replayStream = std::make_unique<ReplayStream>(dataRoot, tickers, replayDay, replayDays, replaySpeed);
        if (!replayStream->load())
            return 1;
        const auto& days = replayStream->getDays();
        std::cout << "\nREPLAY of " << days.front() << (days.size() > 1 ? " to " + days.back() : std::string())
                  << " (" << replayStream->minutes() << " minutes) with a simulated broker, "
                  << (replaySpeed > 0 ? std::to_string(static_cast<int>(replaySpeed)) + " minutes per second"
                                      : std::string("as fast as possible"))
                  << ". Nothing is sent to Alpaca." << std::endl;
    }

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
        std::vector<MarketCondition> history = replay ? replayStream->history(lane->ticker, needed)
                                                      : stream.fetchRecentBars(lane->ticker, needed);
        lane->marketData.update(history);
    }

    // 4. Saved state: reload trades/positions from the last run, then check each stock against what Alpaca holds
    std::string stateDir = dataRoot + (replay ? "replay/" + replayStream->getDays().front()
                                              : (dryRun ? "live_dry_run" : "live"));
    if (replay)
        for (const char* file : {"trades.csv", "positions.csv", "performance.json"})
            std::filesystem::remove(std::filesystem::path(stateDir) / file);   // each replay starts fresh
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

    std::atomic<bool> replayFinished{false};
    std::string replayDayShown;
    double dayStartEquity = simulated.getCurrentEquity();

    auto onBar = [&](const MarketCondition& bar)
    {
        auto found = laneByTicker.find(bar.Ticker);
        if (found == laneByTicker.end())
            return;   // not one of ours
        Lane& lane = *found->second;

        // Hold back anything the decision prints (orders sent, strategy notes): listed under the table
        CaptureStdout decisionOutput;
        auto started = std::chrono::steady_clock::now();

        if (replay)
        {
            if (bar.DateTime.substr(0, 10) != replayDayShown)
            {
                replayDayShown = bar.DateTime.substr(0, 10);
                dayStartEquity = simulated.getCurrentEquity();   // "today" starts here
            }
            simulatedFeed.addBar(bar, 5);   // the simulated broker prices orders off this bar
        }
        lane.marketData.addBar(bar, lane.maxBars);
        lane.engine.run();   // points the shared OMS at this stock's prices, then decides

        double decisionMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        if (replay)
            simulated.nextStep();   // fill this bar's orders, run stop losses / take profits for this stock

        // After the decision (not timed): catch up with fills the broker made itself (stop loss / take profit),
        // update our records and fetch P&L
        oms.setMarketData(lane.marketData);
        oms.syncPosition(bar.Ticker);
        std::string decisionLines = decisionOutput.release();
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
        if (replay)
        {
            account.ok = true;
            account.equity = simulated.getCurrentEquity();
            account.lastEquity = dayStartEquity;
            account.cash = simulated.getCurrentCash();
            Position held = simulated.getLatestPosition(bar.Ticker);
            if (held.getQuantity() != 0)
            {
                std::ostringstream cell;
                cell << held.getQuantity() << " @ " << fixed2(held.getAvgPrice());
                row.position = cell.str();
                double unrealised = (bar.Close - held.getAvgPrice()) * held.getQuantity();
                row.unrealised = money(unrealised) + " (" + (unrealised >= 0 ? "+" : "") +
                                 fixed2(unrealised / std::fabs(held.getAvgPrice() * held.getQuantity()) * 100.0) + "%)";
            }
            performance["simulated"] = {{"equity", account.equity}, {"cash", account.cash}};
        }
        else if (dryRun)
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
            account = alpaca.getAccount();
            alpacaSnapshots[bar.Ticker] = alpacaPosition(alpaca, bar.Ticker, row);
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
        if (dryRun)
            view.estimate = "dry run estimate " + money(summary.totalPnL);
        if (replay)
            view.estimate = "REPLAY";
        std::istringstream lines(decisionLines);
        for (std::string line; std::getline(lines, line);)
            if (!line.empty() && !simulatorBookkeeping(line))
                view.events.push_back(bar.Ticker + ": " + line);

        bool everyStock = true;
        for (const auto& ticker : tickers)
            everyStock = everyStock && view.rows[ticker].seen;
        if (everyStock)
            printMinute(view, tickers, lastPrice);
    };

    if (replay)
        replayStream->start(onBar, [&] { replayFinished = true; running = false; });
    else
        stream.start(onBar);

    // 6. Bars arrive on the stream's own thread; this thread waits for Ctrl+C, and prints a minute's table
    //    if some stock still hasn't reported a few seconds after the first one did
    while (running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::lock_guard<std::mutex> guard(viewLock);
        if (view.open && std::chrono::steady_clock::now() - view.firstBar > waitForStragglers)
            printMinute(view, tickers, lastPrice);
    }

    std::cout << (replayFinished ? "\nReplay finished." : "\nStopping...") << std::endl;
    if (replay)
        replayStream->stop();
    else
        stream.stop();
    {
        std::lock_guard<std::mutex> guard(viewLock);
        printMinute(view, tickers, lastPrice);
    }
    if (replay)
    {
        double start = simulated.getStartingCapital(), equity = simulated.getCurrentEquity();
        std::cout << "Simulated equity " << dollars(equity) << " (" << money(equity - start) << ", "
                  << fixed2((equity / start - 1.0) * 100.0) << "%) from " << dollars(start) << ", "
                  << simulated.getFilledOrders().size() << " fills" << std::endl;
        for (const auto& lane : lanes)
        {
            Position held = simulated.getLatestPosition(lane->ticker);
            int fills = 0;
            for (const auto& order : simulated.getFilledOrders())
                if (order.getTicker() == lane->ticker)
                    fills++;
            std::ostringstream line;
            line << "  " << lane->ticker << ": " << fills << " fills, ";
            if (held.getQuantity() != 0)
                line << "holding " << held.getQuantity() << " @ " << fixed2(held.getAvgPrice());
            else
                line << "flat";
            std::cout << line.str() << std::endl;
        }
    }
    else if (dryRun)
        std::cout << "Dry run estimate: " << money(tracker.summary().totalPnL) << std::endl;
    else
    {
        AlpacaBroker::AccountSnapshot account = alpaca.getAccount();
        if (account.ok)
            std::cout << "Equity " << dollars(account.equity) << " (today "
                      << money(account.equity - account.lastEquity) << ")" << std::endl;
        for (const auto& lane : lanes)
        {
            MinuteRow row;
            alpacaPosition(alpaca, lane->ticker, row);
            std::cout << "  " << lane->ticker << ": " << (row.position == "-" ? "no position"
                         : row.position + ", unrealised " + row.unrealised) << std::endl;
        }
    }
    std::cout << "State saved in " << stateDir << std::endl;
    Log::verbose = false;   // no goodbye notes from the simulated broker
    return 0;
}
