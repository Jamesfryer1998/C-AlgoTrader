#include "LiveTrader.hpp"

#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
#include "../alpaca/AlpacaApi.hpp"
#include "../alpaca/AlpacaStream.hpp"
#include "BacktestDownloader.hpp"
#include "StartupSummary.hpp"
#include "../backtest/BacktestStream.hpp"
#include "../util/Config.hpp"
#include "../util/Defaults.hpp"
#include "../util/Format.hpp"
#include "../util/Log.hpp"
#include "../util/OutputCapture.hpp"

namespace
{
    // How long a minute's table waits for stocks that haven't sent a bar (a quiet stock can have no IEX
    // trades, and so no bar, in a minute)
    const std::chrono::seconds WAIT_FOR_MISSING_BARS{8};

    // How often the main thread checks for Ctrl+C and overdue tables
    const std::chrono::milliseconds CHECK_INTERVAL{200};

    // performance.json is rewritten at most this often (a backtest runs many minutes a second); the last
    // figures are always saved at the end
    const std::chrono::seconds PERFORMANCE_SAVE_INTERVAL{1};

    // The start-up summary's labels
    const std::string MODE_LABEL = "Mode";
    const std::string ACCOUNT_LABEL = "Account";
    const std::string MARKET_LABEL = "Market";
    const std::string BARS_LABEL = "Bars";
    const std::string RISK_LABEL = "Risk";
    const std::string RECORDS_LABEL = "Records";
    const std::string RISK_KEY = "risk";

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

std::ostream&
LiveTrader::screen() const
{
    return options.quiet || options.silent ? Log::silent() : std::cout;
}

std::ostream&
LiveTrader::recap() const
{
    return options.silent ? Log::silent() : std::cout;
}

int
LiveTrader::run()
{
    stopRequested = false;
    if (!loadPortfolio())
        return 1;
    createLanes();
    if (!openFeed() || !connectBroker())
        return 1;
    backfillLanes();
    openRecords();
    printStartup();

    startFeed();
    waitUntilFinished();
    stopFeed();
    savePerformance(true);
    store->flush();   // finish writing the records before the recap
    printResultsHeading();
    session->printSummary(recap(), tickers);
    recap() << capital.describe(config.value("starting_capital", Defaults::STARTING_CAPITAL)) << std::endl;
    recap() << "Records saved in " << recordsDir << std::endl;
    collectResult();
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
        std::cerr << "ERROR - " << e.what() << " (" << ConfigFiles::STRATEGY << ")" << std::endl;
        return false;
    }

    // Alpaca's test feed only streams its fake symbol
    bool testFeed = config.value("alpaca", json::object()).value("data_feed", "") == AlpacaApi::TEST_FEED;
    if (!options.backtest && testFeed)
    {
        if (portfolio.size() > 1)
            screen() << "Test feed: only " << AlpacaApi::TEST_SYMBOL << " streams, so trading it alone with "
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

// Live: Alpaca's WebSocket stream. Backtest: the saved bars of the chosen days (downloading any that are missing).
bool
LiveTrader::openFeed()
{
    if (!options.backtest)
    {
        feed = std::make_unique<AlpacaStream>(config, tickers);
        return true;
    }

    if (!options.backtestDay.empty())
    {
        BacktestDownloader downloader(config, dataRoot);
        if (!downloader.downloadMissing(tickers, options.backtestDay, options.backtestDays))
            return false;
    }

    auto stream = std::make_unique<BacktestStream>(dataRoot, tickers, options.backtestDay, options.backtestDays,
                                                 options.backtestSpeed);
    if (!stream->load())
        return false;
    const auto& days = stream->getDays();
    session = std::make_unique<BacktestSession>(config, days.front());
    result.firstDay = days.front();
    result.lastDay = days.back();
    feed = std::move(stream);
    return true;
}

// Live: Alpaca (check our keys work and the account is reachable). Backtest: the simulated broker.
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
    for (auto& lane : lanes)
        lane->backfill(feed->history(lane->getTicker(), lane->barsNeeded()));
}

// Trades and positions from the last run are reloaded (live), then each stock is checked against the broker.
// A backtest always starts fresh.
void
LiveTrader::openRecords()
{
    recordsDir = dataRoot + session->recordsFolder();
    if (options.backtest)
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

// Everything set up above, in one block (StartupSummary)
void
LiveTrader::printStartup()
{
    StartupSummary summary;
    summary.add(MODE_LABEL, session->describeMode());
    summary.add(ACCOUNT_LABEL, session->describeAccount());
    summary.add(MARKET_LABEL, session->describeMarketHours());
    summary.add(BARS_LABEL, feed->describe());
    summary.add(RISK_LABEL, config.contains(RISK_KEY) ? StartupSummary::describeRisk(config[RISK_KEY])
                                                      : lanes.front()->describeRisk());
    summary.add(RECORDS_LABEL, describeRecords());
    for (const auto& lane : lanes)
        summary.addStock(lane->getTicker(), lane->activeStrategies(), lane->backfilledBars(), lane->barsNeeded());
    summary.print(screen());
}

// e.g. "data/live: 43 orders and 5 positions restored"
std::string
LiveTrader::describeRecords() const
{
    std::string folder = config.value("marketDataBasePath", std::string(Defaults::DATA_FOLDER)) + session->recordsFolder();
    if (options.backtest)
        return folder;
    size_t orders = oms.getRestoredOrders(), positions = oms.getRestoredPositions();
    if (orders == 0 && positions == 0)
        return folder + ": starting fresh";
    return folder + ": " + std::to_string(orders) + " orders and " + std::to_string(positions) + " positions restored";
}

// ---------- Running ----------

void
LiveTrader::startFeed()
{
    feed->start([this](const MarketCondition& bar) { onBar(bar); },
                [this] { feedFinished = true; stopRequested = true; });
}

// Bars arrive on the feed's own thread. This one waits for Ctrl+C (or the end of a backtest), printing a
// minute's table if some stock still hasn't reported a while after the first one did.
void
LiveTrader::waitUntilFinished()
{
    while (!stopRequested)
    {
        std::this_thread::sleep_for(CHECK_INTERVAL);
        std::lock_guard<std::mutex> guard(reportLock);
        if (report->isOverdue(WAIT_FOR_MISSING_BARS))
            report->print(screen());
    }
}

// e.g. "=== Backtest results: 2025-11-03 to 2026-01-14 (50 days) ==="
void
LiveTrader::printResultsHeading() const
{
    if (!options.backtest)
    {
        recap() << "\n=== Session results ===" << std::endl;
        return;
    }
    int days = equityStats.days();
    recap() << "\n=== Backtest results: " << result.firstDay
            << (result.lastDay != result.firstDay ? " to " + result.lastDay : std::string())
            << " (" << days << (days == 1 ? " day" : " days") << ") ===" << std::endl;
}

void
LiveTrader::stopFeed()
{
    if (!feedFinished)
        screen() << "\nStopping..." << std::endl;
    feed->stop();

    std::lock_guard<std::mutex> guard(reportLock);
    report->print(screen());
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
    float held = oms.heldQuantity(bar.Ticker);
    std::optional<Order> order = lane.decide(held);                           // 2. the strategies vote
    if (order)
        order = lane.size(*order, sizingContext(bar.Ticker, held));           // 3. the risk engine sizes it
    if (order)
        sendOrder(lane, *order);                                              // 4. risk checks, then the broker
    double decisionMs = millisecondsSince(started);

    session->afterDecision();
    updateBook(lane, bar);                                                    // 5. positions and P&L
    addToReport(lane, bar, decisionMs, notes.release());                      // 6. the minute's table
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
    capital.update(bar.Ticker, row.position.quantity, bar.Close, bar.DateTime);
    row.decisionMs = decisionMs;
    AccountSnapshot account = session->account().account();
    if (account.available)
    {
        equityStats.update(account.equity, bar.DateTime);
        if (startingEquity <= 0)
            startingEquity = account.equity;
    }
    savedPositions[bar.Ticker] = row.position.toJson();
    lastAccount = account;
    savePerformance(false);

    std::lock_guard<std::mutex> guard(reportLock);
    report->addRow(bar, row, screen());
    report->setAccount(account, session->account().headerNote());
    std::istringstream lines(notes);
    for (std::string line; std::getline(lines, line);)
        if (!line.empty())
            report->addEvent(bar.Ticker, line);
    if (report->isComplete())
        report->print(screen());
}

// data/<records>/performance.json: our estimate, the broker's figures and every position. At most once a
// second while running (unless `now`), so a fast backtest isn't held up writing the same file
void
LiveTrader::savePerformance(bool now)
{
    auto time = std::chrono::steady_clock::now();
    if (!now && time - lastPerformanceSave < PERFORMANCE_SAVE_INTERVAL)
        return;
    lastPerformanceSave = time;
    if (!store)
        return;
    json performance = {{"estimate", PerformanceTracker::toJson(tracker.summary())},
                        {"account", {{"equity", lastAccount.equity}, {"today_pnl", lastAccount.todayPnL()},
                                     {"cash", lastAccount.cash}}},
                        {"positions", savedPositions}};
    store->savePerformance(performance);
}

// What the risk engine needs to size an order for this stock
RiskEngine::SizingContext
LiveTrader::sizingContext(const std::string& ticker, float held)
{
    RiskEngine::SizingContext context;
    context.equity = session->account().account().equity;
    context.heldQuantity = held;
    context.otherPositionsValue = capital.current() - capital.valueOf(ticker);
    context.closedTradeReturns = &tracker.closedTradeReturns(ticker);
    return context;
}

void
LiveTrader::collectResult()
{
    AccountSnapshot account = session->account().account();
    result.startingEquity = startingEquity;
    result.endingEquity = account.available ? account.equity : startingEquity;
    result.maxDrawdown = equityStats.maxDrawdown();
    result.sharpe = equityStats.sharpe();
    result.mostCapitalInUse = capital.peak();
    result.fills = session->fills();
    result.days = equityStats.days();
}
