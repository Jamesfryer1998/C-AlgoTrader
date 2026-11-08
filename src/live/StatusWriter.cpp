#include "StatusWriter.hpp"

#include <ctime>
#include <unistd.h>
#include "../oms/TradeStore.hpp"
#include "../util/MarketTime.hpp"

namespace
{
    const std::chrono::seconds SAVE_INTERVAL{1};
    const size_t MINUTE_LENGTH = 16;   // "YYYY-MM-DD HH:MM"

    json levelsToJson(const StrategyBase::DecisionLevels& levels)
    {
        return {{"label", levels.label}, {"lower", levels.lower}, {"current", levels.current},
                {"upper", levels.upper}, {"state", levels.state}, {"ready", levels.ready}};
    }
}

void
StatusWriter::setRun(const json& value)
{
    std::lock_guard<std::mutex> guard(lock);
    run = value;
}

void
StatusWriter::setState(const std::string& value)
{
    std::lock_guard<std::mutex> guard(lock);
    state = value;
}

void
StatusWriter::setMarketClock(const json& clock)
{
    std::lock_guard<std::mutex> guard(lock);
    marketClock = clock;
}

void
StatusWriter::setConnected(bool value)
{
    std::lock_guard<std::mutex> guard(lock);
    connected = value;
}

void
StatusWriter::updateStock(const std::string& ticker, const std::string& barTime, double price,
                          const std::vector<StrategyBase::DecisionLevels>& strategies,
                          const PositionSnapshot& position, double decisionMs)
{
    json levels = json::array();
    for (const auto& strategy : strategies)
        levels.push_back(levelsToJson(strategy));

    std::lock_guard<std::mutex> guard(lock);
    stocks[ticker] = {{"ticker", ticker}, {"time", barTime}, {"price", price}, {"strategies", levels},
                      {"position", {{"quantity", position.quantity}, {"avg_price", position.averagePrice},
                                    {"unrealised_pnl", position.unrealisedPnL},
                                    {"unrealised_fraction", position.unrealisedFraction},
                                    {"market_value", position.quantity * price}}},
                      {"decision_ms", decisionMs}};
    lastBarTime = std::max(lastBarTime, barTime);
    lastBarReceivedUtc = utcNow();
}

void
StatusWriter::addEvent(const std::string& barTime, const std::string& ticker, const std::string& text)
{
    std::lock_guard<std::mutex> guard(lock);
    events.push_back({{"time", barTime}, {"ticker", ticker}, {"text", text}});
    while (events.size() > MAX_EVENTS)
        events.pop_front();
}

void
StatusWriter::setAccount(const AccountSnapshot& snapshot)
{
    std::lock_guard<std::mutex> guard(lock);
    account = {{"available", snapshot.available}, {"equity", snapshot.equity}, {"cash", snapshot.cash},
               {"today_pnl", snapshot.todayPnL()}};
}

void
StatusWriter::recordEquity(TradeStore& store, const std::string& barTime, double equity)
{
    std::string minute = barTime.substr(0, MINUTE_LENGTH);
    {
        std::lock_guard<std::mutex> guard(lock);
        if (equity <= 0 || minute == lastEquityMinute)
            return;
        lastEquityMinute = minute;
    }
    store.appendEquity(barTime, equity);
}

void
StatusWriter::save(TradeStore& store, bool now, const std::function<json()>& ourEstimate)
{
    {
        std::lock_guard<std::mutex> guard(lock);
        auto time = std::chrono::steady_clock::now();
        if (!now && time - lastSave < SAVE_INTERVAL)
            return;
        lastSave = time;
    }
    json figures = ourEstimate ? ourEstimate() : json::object();
    json status;
    {
        std::lock_guard<std::mutex> guard(lock);
        estimate = figures;
        status = build();
    }
    store.saveStatus(status);
}

json
StatusWriter::toJson() const
{
    std::lock_guard<std::mutex> guard(lock);
    return build();
}

json
StatusWriter::build() const
{
    json stockList = json::array();
    for (const auto& [ticker, stock] : stocks)
        stockList.push_back(stock);
    json eventList = json::array();
    for (const auto& event : events)
        eventList.push_back(event);
    return {{"state", state}, {"pid", static_cast<long>(::getpid())}, {"updated_utc", utcNow()},
            {"run", run}, {"market_clock", marketClock}, {"connected", connected},
            {"last_bar_time", lastBarTime}, {"last_bar_received_utc", lastBarReceivedUtc},
            {"account", account}, {"estimate", estimate}, {"stocks", stockList},
            {"events", eventList}};
}

std::string
StatusWriter::utcNow()
{
    return MarketTime::formatUtc(std::time(nullptr));
}
