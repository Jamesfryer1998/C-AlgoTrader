#include "BrokerSession.hpp"

#include <ctime>

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

    // "09:30 New York (14:30 here)"
    const char* const CLOCK_FORMAT = "%H:%M";
    const char* const DAY_FORMAT = "%a %H:%M";   // when it's not today: "Mon 09:30"
    const char* const WEEKDAY_FORMAT = "%a ";

    std::string localTime(std::time_t utc, bool withDay)
    {
        std::tm tm{};
        localtime_r(&utc, &tm);
        char text[32];
        std::strftime(text, sizeof(text), withDay ? DAY_FORMAT : CLOCK_FORMAT, &tm);
        return text;
    }

    // "09:30" or "Mon 09:30" in New York
    std::string newYorkTime(std::time_t utc, bool withDay)
    {
        std::string dateTime = MarketTime::toNewYorkTime(utc);   // "2026-11-09 09:30:00"
        std::string clock = MarketTime::clock(MarketTime::minuteOfDay(dateTime));
        if (!withDay)
            return clock;
        std::tm tm{};
        std::time_t day = MarketTime::parseDate(MarketTime::datePart(dateTime));
        gmtime_r(&day, &tm);
        char text[16];
        std::strftime(text, sizeof(text), WEEKDAY_FORMAT, &tm);
        return text + clock;
    }

    std::string describe(std::time_t utc, std::time_t now)
    {
        bool today = MarketTime::datePart(MarketTime::toNewYorkTime(utc)) == MarketTime::datePart(MarketTime::toNewYorkTime(now));
        return newYorkTime(utc, !today) + " New York, " + localTime(utc, !today) + " here";
    }
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

// e.g. "New York market opens in 56m (09:30 New York, 14:30 here)"
void
AlpacaSession::printMarketHours(std::ostream& out)
{
    std::time_t now = std::time(nullptr);
    AlpacaBroker::MarketClock clock = alpaca.getMarketClock();
    if (!clock.known)
    {
        // Alpaca's clock unreachable: work it out from regular hours (holidays not known)
        clock.isOpen = MarketTime::isRegularSession(MarketTime::toNewYorkTime(now));
        clock.nextOpen = MarketTime::nextRegularOpen(now);
        clock.nextClose = MarketTime::regularCloseAfter(now);
    }

    if (clock.isOpen)
        out << "New York market is open, closes in " << Format::duration(clock.nextClose - now) << " ("
            << describe(clock.nextClose, now) << ")";
    else
        out << "New York market opens in " << Format::duration(clock.nextOpen - now) << " ("
            << describe(clock.nextOpen, now) << ")";
    out << (clock.known ? "" : " - regular hours, holidays not checked") << std::endl;
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
