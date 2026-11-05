#include "BrokerSession.hpp"

#include "../util/Defaults.hpp"
#include "../util/Format.hpp"
#include "../util/MarketTime.hpp"

namespace
{
    // Where each session's trades, positions and performance go, under data/
    const std::string LIVE_FOLDER = "live";
    const std::string DRY_RUN_FOLDER = "live_dry_run";
    const std::string REPLAY_FOLDER = "replay/";   // + the first replayed day

    // Replay fills: slippage against us on every fill (config "replay_slippage_percent" overrides), no commission
    const double DEFAULT_REPLAY_SLIPPAGE_PERCENT = 0.01;
    const double REPLAY_COMMISSION = 0.0;

    // The simulated broker only needs the bar being processed
    const size_t SIMULATED_BROKER_BARS = 5;

    const double PERCENT = 100.0;
    const int SUMMARY_PERCENT_DECIMALS = 2;
}

// ---------- Alpaca ----------

AlpacaSession::AlpacaSession(const json& config, OrderManagement& oms, PerformanceTracker& tracker)
    : alpaca(config), oms(oms), tracker(tracker)
{
}

bool
AlpacaSession::connect()
{
    if (!alpaca.connect())
        return false;
    if (alpaca.isDryRun())
        monitor = std::make_unique<DryRunAccountMonitor>(oms, tracker);
    else
        monitor = std::make_unique<AlpacaAccountMonitor>(alpaca);
    return true;
}

std::string
AlpacaSession::recordsFolder() const
{
    return alpaca.isDryRun() ? DRY_RUN_FOLDER : LIVE_FOLDER;
}

void
AlpacaSession::printSummary(std::ostream& out, const std::vector<std::string>& tickers)
{
    if (alpaca.isDryRun())
    {
        out << "Dry run estimate: " << Format::dollarChange(tracker.summary().totalPnL) << std::endl;
        return;
    }

    AccountSnapshot snapshot = monitor->account();
    if (snapshot.available)
        out << "Equity " << Format::dollars(snapshot.equity) << " (today " << Format::dollarChange(snapshot.todayPnL())
            << ")" << std::endl;
    for (const auto& ticker : tickers)
    {
        PositionSnapshot held = monitor->position(ticker, 0);
        out << "  " << ticker << ": "
            << (held.isOpen() ? held.describe() + ", unrealised " + held.describePnL() : std::string("no position"))
            << std::endl;
    }
}

// ---------- Replay ----------

ReplaySession::ReplaySession(const json& config, const std::string& firstDay)
    : config(config), firstDay(firstDay), simulated(latestBars), monitor(simulated)
{
}

bool
ReplaySession::connect()
{
    simulated.followLatestBar(true);
    simulated.allowSeveralStocks(true);
    simulated.setSlippageMode(SimulatedBroker::SlippageMode::Adverse);
    simulated.setSlippage(config.value("replay_slippage_percent", DEFAULT_REPLAY_SLIPPAGE_PERCENT) / PERCENT);
    simulated.setCommission(REPLAY_COMMISSION);
    simulated.setStartingCapital(config.value("starting_capital", Defaults::STARTING_CAPITAL));
    return simulated.connect();
}

std::string
ReplaySession::recordsFolder() const
{
    return REPLAY_FOLDER + firstDay;
}

// The simulated broker prices this bar's orders off its close
void
ReplaySession::beforeDecision(const MarketCondition& bar)
{
    monitor.onDay(MarketTime::datePart(bar.DateTime));
    latestBars.addBar(bar, SIMULATED_BROKER_BARS);
}

// Fill this bar's orders, run stop losses / take profits for this stock
void
ReplaySession::afterDecision()
{
    simulated.nextStep();
}

void
ReplaySession::printSummary(std::ostream& out, const std::vector<std::string>& tickers)
{
    double start = simulated.getStartingCapital(), equity = simulated.getCurrentEquity();
    out << "Simulated equity " << Format::dollars(equity) << " (" << Format::dollarChange(equity - start) << ", "
        << Format::percentChange(equity / start - 1.0, SUMMARY_PERCENT_DECIMALS) << ") from " << Format::dollars(start)
        << ", " << simulated.getFilledOrders().size() << " fills" << std::endl;
    for (const auto& ticker : tickers)
    {
        int fills = 0;
        for (const auto& order : simulated.getFilledOrders())
            if (order.getTicker() == ticker)
                fills++;
        PositionSnapshot held = monitor.position(ticker, simulated.getLatestPrice(ticker));
        out << "  " << ticker << ": " << fills << " fills, "
            << (held.isOpen() ? "holding " + held.describe() : std::string("flat")) << std::endl;
    }
}
