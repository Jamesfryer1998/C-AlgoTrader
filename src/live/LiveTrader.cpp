#include "LiveTrader.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>
#include "../util/Format.hpp"
#include "../util/OutputCapture.hpp"

std::atomic<bool> LiveTrader::stopRequested{false};

LiveTrader::LiveTrader(const json& config, const LiveOptions& options, const std::string& dataRoot)
    : config(config),
      options(options),
      dataRoot(dataRoot),
      alpaca(config),
      simulated(simulatedFeed),
      stream(config),
      tracker(config.value("starting_capital", 100000.0))
{
}

int
LiveTrader::run()
{
    if (!connectBroker() || !loadPortfolio())
        return 1;
    createLanes();
    if (options.replay && !loadReplay())
        return 1;
    backfillLanes();
    openRecords();

    MinuteReport::printLegend(std::cout);
    startBars();
    waitUntilFinished();
    stopBars();
    printFinalSummary();
    return 0;
}

// Live: Alpaca (check our keys work and the account is reachable). Replay: a simulated broker that fills
// orders at each bar's close (plus slippage) and runs stop losses / take profits itself.
bool
LiveTrader::connectBroker()
{
    if (options.replay)
    {
        simulated.followLatestBar(true);
        simulated.tradeSeveralStocks(true);
        simulated.setSlippageMode(SimulatedBroker::SlippageMode::Adverse);
        simulated.setSlippage(config.value("replay_slippage_percent", DEFAULT_REPLAY_SLIPPAGE_PERCENT) / 100.0);
        simulated.setCommission(0.0);
        simulated.setStartingCapital(config.value("starting_capital", 100000.0));

        broker = &simulated;
        auto replayAccount = std::make_unique<SimulatedAccountMonitor>(simulated);
        replayMonitor = replayAccount.get();
        monitor = std::move(replayAccount);
        return true;
    }

    if (!alpaca.connect())
        return false;
    broker = &alpaca;
    if (alpaca.isDryRun())
        monitor = std::make_unique<DryRunAccountMonitor>(oms, tracker);
    else
        monitor = std::make_unique<AlpacaAccountMonitor>(alpaca);
    return true;
}

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
    if (!options.replay && stream.getFeed() == "test")
    {
        if (portfolio.size() > 1)
            std::cout << "Test feed: only " << stream.getSymbol() << " streams, so trading it alone with "
                      << portfolio.front().ticker << "'s strategies" << std::endl;
        portfolio.resize(1);
        portfolio.front().ticker = stream.getSymbol();
    }
    return true;
}

// One lane per stock, all sharing one OMS
void
LiveTrader::createLanes()
{
    oms.setUp(config, broker);
    for (const auto& entry : portfolio)
    {
        auto lane = std::make_unique<StockLane>(entry, oms, *broker);
        laneByTicker[lane->getTicker()] = lane.get();
        tickers.push_back(lane->getTicker());
        lanes.push_back(std::move(lane));
    }
    stream.setSymbols(tickers);
    report = std::make_unique<MinuteReport>(tickers);
}

bool
LiveTrader::loadReplay()
{
    replay = std::make_unique<ReplayStream>(dataRoot, tickers, options.replayDay, options.replayDays, options.replaySpeed);
    if (!replay->load())
        return false;

    const auto& days = replay->getDays();
    std::cout << "\nREPLAY of " << days.front() << (days.size() > 1 ? " to " + days.back() : std::string())
              << " (" << replay->minutes() << " minutes) with a simulated broker, "
              << (options.replaySpeed > 0 ? Format::number(options.replaySpeed, 0) + " minutes per second"
                                          : std::string("as fast as possible"))
              << ". Nothing is sent to Alpaca." << std::endl;
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
        int needed = lane->barsNeeded();
        lane->backfill(replay ? replay->history(lane->getTicker(), needed)
                              : stream.fetchRecentBars(lane->getTicker(), needed));
    }
}

// Trades and positions from the last run are reloaded (live), then each stock is checked against the broker.
// A replay always starts fresh.
void
LiveTrader::openRecords()
{
    std::string folder = replay ? "replay/" + replay->getDays().front() : (alpaca.isDryRun() ? "live_dry_run" : "live");
    recordsDir = dataRoot + folder;
    if (replay)
        for (const char* file : {"trades.csv", "positions.csv", "performance.json"})
            std::filesystem::remove(std::filesystem::path(recordsDir) / file);

    store = std::make_unique<TradeStore>(recordsDir);
    oms.setMarketData(lanes.front()->getMarketData());
    oms.enablePersistence(store.get(), &tracker);
    for (auto& lane : lanes)
    {
        oms.setMarketData(lane->getMarketData());
        oms.syncPosition(lane->getTicker());
    }
}

void
LiveTrader::startBars()
{
    auto onEachBar = [this](const MarketCondition& bar) { onBar(bar); };
    if (replay)
        replay->start(onEachBar, [this] { replayFinished = true; stopRequested = true; });
    else
        stream.start(onEachBar);
}

// Bars arrive on the stream's own thread. This one waits for Ctrl+C (or the end of a replay), printing a
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
LiveTrader::stopBars()
{
    std::cout << (replayFinished ? "\nReplay finished." : "\nStopping...") << std::endl;
    if (replay)
        replay->stop();
    else
        stream.stop();

    std::lock_guard<std::mutex> guard(reportLock);
    report->print(std::cout);
}

void
LiveTrader::onBar(const MarketCondition& bar)
{
    auto found = laneByTicker.find(bar.Ticker);
    if (found == laneByTicker.end())
        return;   // not one of ours
    StockLane& lane = *found->second;

    // Anything the decision prints (orders sent, rejections, strategy notes) is listed under the table
    OutputCapture decisionOutput;
    if (replayMonitor)
    {
        replayMonitor->onDay(bar.DateTime.substr(0, 10));
        simulatedFeed.addBar(bar, SIMULATED_BROKER_BARS);   // the simulated broker prices orders off this bar
    }
    double decisionMs = lane.decide(bar);
    if (replay)
        simulated.nextStep();   // fill this bar's orders, run stop losses / take profits for this stock

    // After the decision (not timed): catch up with fills the broker made itself (stop loss / take profit)
    oms.setMarketData(lane.getMarketData());
    oms.syncPosition(bar.Ticker);
    tracker.markPrice(bar.Ticker, bar.Close);
    recordEquityOncePerMinute(bar);
    std::string decisionNotes = decisionOutput.release();

    MinuteReport::StockRow row;
    row.price = bar.Close;
    row.strategies = lane.strategyLevels();
    row.position = monitor->position(bar.Ticker, bar.Close);
    row.decisionMs = decisionMs;
    AccountSnapshot account = monitor->account();

    savedPositions[bar.Ticker] = row.position.toJson();
    json performance = {{"estimate", PerformanceTracker::toJson(tracker.summary())},
                        {"account", {{"equity", account.equity}, {"today_pnl", account.todayPnL()}, {"cash", account.cash}}},
                        {"positions", savedPositions}};
    store->savePerformance(performance);

    std::lock_guard<std::mutex> guard(reportLock);
    report->addRow(bar, row, std::cout);
    report->setAccount(account, monitor->headerNote());
    std::istringstream lines(decisionNotes);
    for (std::string line; std::getline(lines, line);)
        if (!line.empty())
            report->addEvent(bar.Ticker, line);
    if (report->isComplete())
        report->print(std::cout);
}

// Several stocks report in each minute; the P&L book records equity once per minute
void
LiveTrader::recordEquityOncePerMinute(const MarketCondition& bar)
{
    if (bar.DateTime == lastEquityMinute)
        return;
    tracker.recordEquity();
    lastEquityMinute = bar.DateTime;
}

void
LiveTrader::printFinalSummary()
{
    if (replay)
    {
        double start = simulated.getStartingCapital(), equity = simulated.getCurrentEquity();
        std::cout << "Simulated equity " << Format::dollars(equity) << " (" << Format::dollarChange(equity - start)
                  << ", " << Format::percentChange(equity / start - 1.0, 2) << ") from " << Format::dollars(start)
                  << ", " << simulated.getFilledOrders().size() << " fills" << std::endl;
        for (const auto& ticker : tickers)
        {
            int fills = 0;
            for (const auto& order : simulated.getFilledOrders())
                if (order.getTicker() == ticker)
                    fills++;
            PositionSnapshot held = monitor->position(ticker, simulated.getLatestPrice(ticker));
            std::cout << "  " << ticker << ": " << fills << " fills, "
                      << (held.isOpen() ? "holding " + held.describe() : std::string("flat")) << std::endl;
        }
    }
    else if (alpaca.isDryRun())
    {
        std::cout << "Dry run estimate: " << Format::dollarChange(tracker.summary().totalPnL) << std::endl;
    }
    else
    {
        AccountSnapshot account = monitor->account();
        if (account.available)
            std::cout << "Equity " << Format::dollars(account.equity) << " (today "
                      << Format::dollarChange(account.todayPnL()) << ")" << std::endl;
        for (const auto& ticker : tickers)
        {
            PositionSnapshot held = monitor->position(ticker, 0);
            std::cout << "  " << ticker << ": "
                      << (held.isOpen() ? held.describe() + ", unrealised " + held.describePnL() : std::string("no position"))
                      << std::endl;
        }
    }
    std::cout << "Records saved in " << recordsDir << std::endl;
}
