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
    const std::string BACKTEST_FOLDER = "backtest/";   // + the first backtested day

    // Backtest fills: slippage against us on every fill (config "backtest_slippage_percent" overrides), no commission
    const double DEFAULT_BACKTEST_SLIPPAGE_PERCENT = 0.01;
    const double BACKTEST_COMMISSION = 0.0;

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

AlpacaSession::AlpacaSession(const json& config, OrderManagement& oms, PerformanceTracker& tracker,
                             std::shared_ptr<AlpacaClient> client)
    : alpaca(config, client), oms(oms), tracker(tracker)
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

std::string
AlpacaSession::describeMode() const
{
    if (alpaca.isDryRun())
        return "DRY RUN on Alpaca: orders are printed, not sent";
    return alpaca.isPaper() ? "LIVE on Alpaca's paper account" : "LIVE on Alpaca, REAL MONEY (" + alpaca.getTradingUrl() + ")";
}

// e.g. "ACTIVE, cash $98,150.70, buying power $396,385.69"
std::string
AlpacaSession::describeAccount() const
{
    const AlpacaBroker::AccountStatus& account = alpaca.getConnectedAccount();
    return account.status + ", cash " + Format::dollars(account.cash) + ", buying power "
         + Format::dollars(account.buyingPower);
}

// e.g. "closed, opens in 56m (09:30 New York, 14:30 here)"
std::string
AlpacaSession::describeMarketHours()
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

    std::string hours = clock.isOpen
        ? "open, closes in " + Format::duration(clock.nextClose - now) + " (" + describe(clock.nextClose, now) + ")"
        : "closed, opens in " + Format::duration(clock.nextOpen - now) + " (" + describe(clock.nextOpen, now) + ")";
    return hours + (clock.known ? "" : " - regular hours, holidays not checked");
}

json
AlpacaSession::marketClock()
{
    AlpacaBroker::MarketClock clock = alpaca.getMarketClock();
    if (!clock.known)
        return {{"known", false}};
    return {{"known", true}, {"is_open", clock.isOpen}, {"next_open_utc", MarketTime::formatUtc(clock.nextOpen)},
            {"next_close_utc", MarketTime::formatUtc(clock.nextClose)}};
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

// ---------- Backtest ----------

BacktestSession::BacktestSession(const json& config, const std::string& firstDay)
    : config(config), firstDay(firstDay), simulated(latestBars), monitor(simulated)
{
}

bool
BacktestSession::connect()
{
    simulated.followLatestBar(true);
    simulated.allowSeveralStocks(true);
    simulated.setSlippageMode(SimulatedBroker::SlippageMode::Adverse);
    simulated.setSlippage(config.value("backtest_slippage_percent", DEFAULT_BACKTEST_SLIPPAGE_PERCENT) / PERCENT);
    simulated.setCommission(BACKTEST_COMMISSION);
    simulated.setRegulatoryFees(RegulatoryFees::fromConfig(config));
    simulated.setStartingCapital(config.value("starting_capital", Defaults::STARTING_CAPITAL));
    return true;   // the simulated broker connects itself when it's created
}

std::string
BacktestSession::recordsFolder() const
{
    return BACKTEST_FOLDER + firstDay;
}

std::string
BacktestSession::describeMode() const
{
    return "BACKTEST with a simulated broker: nothing is sent to Alpaca";
}

// e.g. "simulated, $100,000.00 to start, 0.01% slippage a fill, SEC and FINRA fees on sells"
std::string
BacktestSession::describeAccount() const
{
    return "simulated, " + Format::dollars(simulated.getStartingCapital()) + " to start, "
         + Format::number(simulated.getSlippagePercentage() * PERCENT, 2) + "% slippage a fill, SEC and FINRA fees on sells";
}

// The simulated broker prices this bar's orders off its close
void
BacktestSession::beforeDecision(const MarketCondition& bar)
{
    monitor.onDay(MarketTime::datePart(bar.DateTime));
    latestBars.addBar(bar, SIMULATED_BROKER_BARS);
}

// Fill this bar's orders, run stop losses / take profits for this stock
void
BacktestSession::afterDecision()
{
    simulated.nextStep();
}

void
BacktestSession::printSummary(std::ostream& out, const std::vector<std::string>& tickers)
{
    double start = simulated.getStartingCapital(), equity = simulated.getCurrentEquity();
    out << "Simulated equity " << Format::dollars(equity) << " (" << Format::dollarChange(equity - start) << ", "
        << Format::percentChange(equity / start - 1.0, SUMMARY_PERCENT_DECIMALS) << ") from " << Format::dollars(start)
        << ", " << simulated.getFilledOrders().size() << " fills, "
        << Format::dollars(simulated.getRegulatoryFeesPaid()) << " in SEC and FINRA fees" << std::endl;
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
