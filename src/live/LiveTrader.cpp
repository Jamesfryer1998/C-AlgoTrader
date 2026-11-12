#include "LiveTrader.hpp"

#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
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
#include "../util/MarketTime.hpp"
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
    const std::string PORTFOLIO = "portfolio";   // the stocks-and-strategies list in strategy.json (and --portfolio)
    // A portfolio chosen to trade instead of config/strategy.json's (the dashboard's "Trade these picks"), kept in
    // the data folder so deploys, which replace config/ from the repo, don't undo it: {"source": ..., "portfolio": [...]}
    const std::string ACTIVE_PORTFOLIO_FILE = "portfolio.json";
    const std::string PORTFOLIO_SOURCE = "source";
    const std::string RISK_KEY = "risk";
    const std::string STARTING_CAPITAL_KEY = "starting_capital";

    // The dashboard's status: the market clock is asked for again this often (it's an Alpaca request)
    const std::chrono::minutes MARKET_CLOCK_REFRESH{5};
    const std::string RUNNING = "running", STOPPING = "stopping", STOPPED = "stopped";

    // Flattening: the closing orders' stop loss and take profit (the risk checks want both; they close
    // the whole position, so nothing is left for them to protect)
    const float FLATTEN_PROTECTION_PERCENT = 5;
    const std::string FLATTEN_STRATEGY = "flatten";

    double millisecondsSince(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
}

std::atomic<bool> LiveTrader::stopRequested{false};
std::atomic<bool> LiveTrader::flattenRequested{false};

LiveTrader::LiveTrader(const json& config, const LiveOptions& options, const std::string& dataRoot)
    : config(withOptions(config, options)),
      options(options),
      dataRoot(dataRoot),
      tracker(this->config.value(STARTING_CAPITAL_KEY, Defaults::STARTING_CAPITAL))
{
}

// The config with anything the command line overrides, e.g. --capital
json
LiveTrader::withOptions(json config, const LiveOptions& options)
{
    if (options.backtest && options.backtestCapital > 0)
        config[STARTING_CAPITAL_KEY] = options.backtestCapital;
    return config;
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
    flattenRequested = false;
    if (!loadPortfolio())
        return 1;
    createLanes();
    if (!openFeed() || !connectBroker())
        return 1;
    backfillLanes();
    openRecords();
    printStartup();

    startFeed();
    status.setState(RUNNING);
    saveStatus(true);
    waitUntilFinished();
    stopFeed();
    savePerformance(true);
    status.setState(STOPPED);
    saveStatus(true);
    store->flush();   // finish writing the records before the recap
    printResultsHeading();
    session->printSummary(recap(), tickers);
    recap() << capital.describe(config.value(STARTING_CAPITAL_KEY, Defaults::STARTING_CAPITAL)) << std::endl;
    recap() << "Records saved in " << recordsDir << std::endl;
    collectResult();
    return 0;
}

// ---------- Setting up ----------

// Trade a file's stocks and strategies instead of config/strategy.json's: a backtest's --portfolio, or the
// portfolio chosen on the dashboard (data/portfolio.json)
void
LiveTrader::useOtherPortfolio(const std::string& path)
{
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Can't open the portfolio file " + path);
    json other = json::parse(file, nullptr, true, true);
    config[PORTFOLIO] = other.is_array() ? other : other.at(PORTFOLIO);
    portfolioSource = other.is_object() && other.contains(PORTFOLIO_SOURCE) ? other[PORTFOLIO_SOURCE].get<std::string>()
                                                                            : path;
}

// The portfolio file to trade instead of config/strategy.json's, if any: --portfolio, else data/portfolio.json
std::string
LiveTrader::otherPortfolioFile() const
{
    if (!options.backtestPortfolio.empty())
        return options.backtestPortfolio;
    std::string chosen = dataRoot + ACTIVE_PORTFOLIO_FILE;
    return std::filesystem::exists(chosen) ? chosen : "";
}

// Where this run's records go, under the data folder: the session's own, or a backtest's --records
std::string
LiveTrader::recordsFolder() const
{
    if (options.backtestRecords.empty())
        return session->recordsFolder();
    std::string folder = options.backtestRecords;
    while (!folder.empty() && folder.back() == '/')
        folder.pop_back();
    return folder;
}

bool
LiveTrader::loadPortfolio()
{
    std::string other = otherPortfolioFile();
    try
    {
        if (!other.empty())
            useOtherPortfolio(other);
        portfolio = Portfolio::fromConfig(config);
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR - " << e.what() << " (" << (other.empty() ? ConfigFiles::STRATEGY : other) << ")" << std::endl;
        return false;
    }
    if (options.backtestPortfolio.empty() && !other.empty())
        screen() << "Trading " << portfolioSource << " (" << other << "), not config/strategy.json's portfolio" << std::endl;

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
    auto backtest = std::make_unique<BacktestSession>(config, days.front());
    backtestSession = backtest.get();
    session = std::move(backtest);
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
    recordsDir = dataRoot + recordsFolder();
    if (options.backtest)
        TradeStore::removeFiles(recordsDir);

    store = std::make_unique<TradeStore>(recordsDir);
    oms.setMarketData(lanes.front()->getMarketData());
    oms.enablePersistence(store.get(), &tracker);
    carryPositionsIn();
    for (auto& lane : lanes)
    {
        oms.setMarketData(lane->getMarketData());
        oms.syncPosition(lane->getTicker());
    }
}

// A backtest given --positions starts holding them, each valued at its last close before the backtest (the
// back-filled bars), so the account starts at its capital as the live one did
void
LiveTrader::carryPositionsIn()
{
    if (!backtestSession)
        return;
    for (const auto& carried : options.backtestPositions)
    {
        auto lane = laneByTicker.find(carried.ticker);
        if (lane == laneByTicker.end())
        {
            std::cerr << "WARNING - " << carried.ticker << " isn't in config/strategy.json's portfolio, so the "
                      << "backtest doesn't hold it" << std::endl;
            continue;
        }
        const MarketData& bars = lane->second->getMarketData();
        double lastClose = bars.getData().empty() ? carried.averagePrice : bars.getLastClosePrice();
        backtestSession->startWithPosition(carried.ticker, carried.quantity, carried.averagePrice, lastClose);
        oms.setMarketData(bars);
        oms.recordCarriedPosition(carried.ticker, carried.quantity, carried.averagePrice);
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
    describeRunForStatus();
}

// The startup summary's facts, for the dashboard
void
LiveTrader::describeRunForStatus()
{
    if (!writesStatus())
        return;
    json stocks = json::array();
    for (const auto& lane : lanes)
        stocks.push_back({{"ticker", lane->getTicker()}, {"strategies", lane->activeStrategies()},
                          {"bars_needed", lane->barsNeeded()}, {"backfilled", lane->backfilledBars()}});
    status.setRun({{"mode", session->describeMode()}, {"account", session->describeAccount()},
                   {"market", session->describeMarketHours()}, {"bars", feed->describe()},
                   {"risk", config.contains(RISK_KEY) ? StartupSummary::describeRisk(config[RISK_KEY])
                                                      : lanes.front()->describeRisk()},
                   {"records", describeRecords()}, {"backtest", options.backtest}, {"portfolio", portfolioSource},
                   {"starting_capital", config.value(STARTING_CAPITAL_KEY, Defaults::STARTING_CAPITAL)},
                   {"started_utc", MarketTime::formatUtc(std::time(nullptr))}, {"stocks", stocks}});
    status.setMarketClock(session->marketClock());
    lastClockCheck = std::chrono::steady_clock::now();
}

void
LiveTrader::saveStatus(bool now)
{
    if (writesStatus())
        status.save(*store, now, [this] { return PerformanceTracker::toJson(tracker.summary()); });
}

// From the main thread, about every CHECK_INTERVAL: a heartbeat for the dashboard (even with no bars, e.g.
// with the market closed), the stream's health, and the market clock now and then
void
LiveTrader::keepStatusFresh()
{
    if (!writesStatus())
        return;
    status.setConnected(feed->isConnected());
    auto now = std::chrono::steady_clock::now();
    if (now - lastClockCheck >= MARKET_CLOCK_REFRESH)
    {
        lastClockCheck = now;
        status.setMarketClock(session->marketClock());
    }
    saveStatus(false);
}

// The kill switch: close every position at its last price, then the caller stops trading
void
LiveTrader::flattenAll()
{
    std::lock_guard<std::mutex> guard(tradingLock);
    screen() << "\nFlatten requested: closing every position, then stopping" << std::endl;
    for (auto& lane : lanes)
    {
        const std::string& ticker = lane->getTicker();
        float held = oms.heldQuantity(ticker);
        if (held == 0 || lane->getMarketData().getData().empty())
            continue;
        float price = lane->getMarketData().getLastClosePrice();
        Order exit(held > 0 ? OrderType::SELL : OrderType::BUY, ticker, std::fabs(held), price);
        exit.setStopLoss(FLATTEN_PROTECTION_PERCENT);
        exit.setTakeProfit(FLATTEN_PROTECTION_PERCENT);
        exit.setStrategy(FLATTEN_STRATEGY);
        OutputCapture notes;
        sendOrder(*lane, exit);
        std::string sent = notes.release();
        screen() << sent;
        status.addEvent(lane->getMarketData().getCurrentData().DateTime, ticker,
                        "Flatten: " + (held > 0 ? std::string("sell ") : std::string("buy ")) + Format::number(std::fabs(held), 0)
                        + " at ~" + Format::number(price));
    }
    saveStatus(true);
}

// e.g. "data/live: 43 orders and 5 positions restored"
std::string
LiveTrader::describeRecords() const
{
    std::string folder = config.value("marketDataBasePath", std::string(Defaults::DATA_FOLDER)) + recordsFolder();
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
        if (flattenRequested.exchange(false))
        {
            flattenAll();
            stopRequested = true;
            break;
        }
        {
            std::lock_guard<std::mutex> guard(reportLock);
            if (report->isOverdue(WAIT_FOR_MISSING_BARS))
                report->print(screen());
        }
        keepStatusFresh();
        session->keepConnectionWarm();
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
    status.setState(STOPPING);
    saveStatus(true);
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
    std::lock_guard<std::mutex> trading(tradingLock);

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

    if (writesStatus())
    {
        status.updateStock(bar.Ticker, bar.DateTime, bar.Close, row.strategies, row.position, decisionMs);
        std::istringstream noteLines(notes);
        for (std::string line; std::getline(noteLines, line);)
            if (!line.empty())
                status.addEvent(bar.DateTime, bar.Ticker, line);
        status.setAccount(account);
        if (account.available)
            status.recordEquity(*store, bar.DateTime, account.equity);
        saveStatus(false);
    }

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
