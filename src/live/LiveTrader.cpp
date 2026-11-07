#include "LiveTrader.hpp"

#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
#include "../alpaca/AlpacaApi.hpp"
#include "../alpaca/AlpacaStream.hpp"
#include "ReplayDownloader.hpp"
#include "../backtest/ReplayStream.hpp"
#include "../util/Defaults.hpp"
#include "../util/Format.hpp"
#include "../util/OutputCapture.hpp"

namespace
{
    // How long a minute's table waits for stocks that haven't sent a bar (a quiet stock can have no IEX
    // trades, and so no bar, in a minute)
    const std::chrono::seconds WAIT_FOR_MISSING_BARS{8};

    // How often the main thread checks for Ctrl+C and overdue tables
    const std::chrono::milliseconds CHECK_INTERVAL{200};

    double millisecondsSince(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
}

std::atomic<bool> LiveTrader::stopRequested{false};

LiveTrader::LiveTrader(const json& config, const LiveOptions& options, const std::string& dataRoot)
    : config(config),
      options(options),
      dataRoot(dataRoot),
      tracker(config.value("starting_capital", Defaults::STARTING_CAPITAL))
{
}

int
LiveTrader::run()
{
    if (!loadPortfolio())
        return 1;
    createLanes();
    if (!openFeed() || !connectBroker())
        return 1;
    backfillLanes();
    openRecords();
    session->printMarketHours(std::cout);

    startFeed();
    waitUntilFinished();
    stopFeed();
    session->printSummary(std::cout, tickers);
    std::cout << "Records saved in " << recordsDir << std::endl;
    return 0;
}

// ---------- Setting up ----------

bool
LiveTrader::loadPortfolio()
{
    try
    {
        portfolio = Portfolio::fromConfig(config);
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR - " << e.what() << " (src/config/config.json)" << std::endl;
        return false;
    }

    // Alpaca's test feed only streams its fake symbol
    bool testFeed = config.value("alpaca", json::object()).value("data_feed", "") == AlpacaApi::TEST_FEED;
    if (!options.replay && testFeed)
    {
        if (portfolio.size() > 1)
            std::cout << "Test feed: only " << AlpacaApi::TEST_SYMBOL << " streams, so trading it alone with "
                      << portfolio.front().ticker << "'s strategies" << std::endl;
        portfolio.resize(1);
        portfolio.front().ticker = AlpacaApi::TEST_SYMBOL;
    }
    return true;
}

// One lane per stock: its prices and strategies
void
LiveTrader::createLanes()
{
    for (const auto& entry : portfolio)
    {
        auto lane = std::make_unique<StockLane>(entry);
        laneByTicker[lane->getTicker()] = lane.get();
        tickers.push_back(lane->getTicker());
        lanes.push_back(std::move(lane));
    }
    report = std::make_unique<MinuteReport>(tickers);
}

// Live: Alpaca's WebSocket stream. Replay: the saved bars of the chosen days (downloading any that are missing).
bool
LiveTrader::openFeed()
{
    if (!options.replay)
    {
        auto stream = std::make_unique<AlpacaStream>(config);
        stream->setSymbols(tickers);
        feed = std::move(stream);
        return true;
    }

    if (!options.replayDay.empty())
    {
        ReplayDownloader downloader(config, dataRoot);
        if (!downloader.downloadMissing(tickers, options.replayDay, options.replayDays))
            return false;
    }

    auto replay = std::make_unique<ReplayStream>(dataRoot, tickers, options.replayDay, options.replayDays,
                                                 options.replaySpeed);
    if (!replay->load())
        return false;
    const auto& days = replay->getDays();
    std::cout << "\nREPLAY of " << days.front() << (days.size() > 1 ? " to " + days.back() : std::string())
              << " (" << replay->minutes() << " minutes) with a simulated broker, "
              << (options.replaySpeed > 0 ? Format::number(options.replaySpeed, 0) + " minutes per second"
                                          : std::string("as fast as possible"))
              << ". Nothing is sent to Alpaca." << std::endl;
    session = std::make_unique<ReplaySession>(config, days.front());
    feed = std::move(replay);
    return true;
}

// Live: Alpaca (check our keys work and the account is reachable). Replay: the simulated broker.
bool
LiveTrader::connectBroker()
{
    if (!session)
        session = std::make_unique<AlpacaSession>(config, oms, tracker);
    if (!session->connect())
        return false;
    oms.setUp(config, &session->broker());
    return true;
}

// Each stock gets exactly the bars its strategies need (e.g. MEANREV lookback 120 needs 120), so it can
// decide from the first new bar
void
LiveTrader::backfillLanes()
{
    std::cout << "Trading " << lanes.size() << " stock(s):" << std::endl;
    for (auto& lane : lanes)
    {
        std::cout << "  " << lane->getTicker() << ": " << lane->strategyNames() << std::endl;
        lane->backfill(feed->history(lane->getTicker(), lane->barsNeeded()));
    }
}

// Trades and positions from the last run are reloaded (live), then each stock is checked against the broker.
// A replay always starts fresh.
void
LiveTrader::openRecords()
{
    recordsDir = dataRoot + session->recordsFolder();
    if (options.replay)
        TradeStore::removeFiles(recordsDir);

    store = std::make_unique<TradeStore>(recordsDir);
    oms.setMarketData(lanes.front()->getMarketData());
    oms.enablePersistence(store.get(), &tracker);
    for (auto& lane : lanes)
    {
        oms.setMarketData(lane->getMarketData());
        oms.syncPosition(lane->getTicker());
    }
}

// ---------- Running ----------

void
LiveTrader::startFeed()
{
    feed->start([this](const MarketCondition& bar) { onBar(bar); },
                [this] { feedFinished = true; stopRequested = true; });
}

// Bars arrive on the feed's own thread. This one waits for Ctrl+C (or the end of a replay), printing a
// minute's table if some stock still hasn't reported a while after the first one did.
void
LiveTrader::waitUntilFinished()
{
    while (!stopRequested)
    {
        std::this_thread::sleep_for(CHECK_INTERVAL);
        std::lock_guard<std::mutex> guard(reportLock);
        if (report->isOverdue(WAIT_FOR_MISSING_BARS))
            report->print(std::cout);
    }
}

void
LiveTrader::stopFeed()
{
    std::cout << (feedFinished ? "\nReplay finished." : "\nStopping...") << std::endl;
    feed->stop();

    std::lock_guard<std::mutex> guard(reportLock);
    report->print(std::cout);
}

// ---------- Each bar ----------

void
LiveTrader::onBar(const MarketCondition& bar)
{
    auto found = laneByTicker.find(bar.Ticker);
    if (found == laneByTicker.end())
        return;   // not one of ours
    StockLane& lane = *found->second;

    OutputCapture notes;   // orders sent, rejections, strategy notes: listed under the table
    session->beforeDecision(bar);

    auto started = std::chrono::steady_clock::now();
    lane.addBar(bar);                                                         // 1. the new price
    std::optional<Order> order = lane.decide(oms.heldQuantity(bar.Ticker));   // 2. the strategies vote
    if (order)
        sendOrder(lane, *order);                                              // 3. risk checks, then the broker
    double decisionMs = millisecondsSince(started);

    session->afterDecision();
    updateBook(lane, bar);                                                    // 4. positions and P&L
    addToReport(lane, bar, decisionMs, notes.release());                      // 5. the minute's table
}

void
LiveTrader::sendOrder(const StockLane& lane, Order& order)
{
    oms.setMarketData(lane.getMarketData());   // the risk checks compare the order with this stock's last close
    oms.sendOrder(order);
}

// Catch up with fills the broker made itself (stop loss / take profit) and value the position. Several stocks
// report each minute; the P&L book records equity once per minute.
void
LiveTrader::updateBook(const StockLane& lane, const MarketCondition& bar)
{
    oms.setMarketData(lane.getMarketData());
    oms.syncPosition(bar.Ticker);
    tracker.markPrice(bar.Ticker, bar.Close);
    if (bar.DateTime != lastEquityMinute)
    {
        tracker.recordEquity();
        lastEquityMinute = bar.DateTime;
    }
}

void
LiveTrader::addToReport(const StockLane& lane, const MarketCondition& bar, double decisionMs, const std::string& notes)
{
    MinuteReport::StockRow row;
    row.price = bar.Close;
    row.strategies = lane.decisionLevels();
    row.position = session->account().position(bar.Ticker, bar.Close);
    row.decisionMs = decisionMs;
    AccountSnapshot account = session->account().account();
    savePerformance(bar, row.position, account);

    std::lock_guard<std::mutex> guard(reportLock);
    report->addRow(bar, row, std::cout);
    report->setAccount(account, session->account().headerNote());
    std::istringstream lines(notes);
    for (std::string line; std::getline(lines, line);)
        if (!line.empty())
            report->addEvent(bar.Ticker, line);
    if (report->isComplete())
        report->print(std::cout);
}

// data/<records>/performance.json: our estimate, the broker's figures and every position
void
LiveTrader::savePerformance(const MarketCondition& bar, const PositionSnapshot& position, const AccountSnapshot& account)
{
    savedPositions[bar.Ticker] = position.toJson();
    json performance = {{"estimate", PerformanceTracker::toJson(tracker.summary())},
                        {"account", {{"equity", account.equity}, {"today_pnl", account.todayPnL()}, {"cash", account.cash}}},
                        {"positions", savedPositions}};
    store->savePerformance(performance);
}
