#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>
#include "AccountMonitor.hpp"
#include "../broker/AccountSnapshot.hpp"
#include "../strategy_engine/StrategyBase.hpp"

using json = nlohmann::json;

class TradeStore;

/**
 * StatusWriter
 *
 * What the trader is doing right now, for the web dashboard (dashboard/), saved in the records folder:
 *
 *   status.json   rewritten at most once a second: state (starting / running / stopping / stopped), the
 *                 process id, mode, account, market clock, stream health, each stock's price, strategy levels,
 *                 position and decision time, and the latest events (orders sent, blocked, rejected)
 *   equity.csv    one line a minute: time,equity
 *
 * The trader works the same without anyone reading these. Bars arrive on the feed's thread while start-up,
 * stopping and the market clock happen on the main one, so every method locks.
 */
class StatusWriter
{
    public:
        static constexpr size_t MAX_EVENTS = 100;

        // About the run, set once at start-up: mode, account, bars, risk, records, tickers, ...
        void setRun(const json& run);
        void setState(const std::string& state);
        void setMarketClock(const json& clock);
        void setConnected(bool connected);

        // One stock's newest bar, as the minute table shows it
        void updateStock(const std::string& ticker, const std::string& barTime, double price,
                         const std::vector<StrategyBase::DecisionLevels>& strategies,
                         const PositionSnapshot& position, double decisionMs);
        void addEvent(const std::string& barTime, const std::string& ticker, const std::string& text);
        void setAccount(const AccountSnapshot& account);

        // Once a minute: the account's value goes in equity.csv
        void recordEquity(TradeStore& store, const std::string& barTime, double equity);

        // Write status.json, at most once a second unless `now`. estimate() gives our own P&L figures (only
        // asked for when the file is written: they take a moment to work out)
        void save(TradeStore& store, bool now, const std::function<json()>& estimate);

        json toJson() const;   // what save() writes (for tests)

    private:
        static std::string utcNow();
        json build() const;

        mutable std::mutex lock;
        json run = json::object();
        std::string state = "starting";
        json marketClock = json::object();
        bool connected = true;
        std::string lastBarTime;
        std::string lastBarReceivedUtc;
        std::map<std::string, json> stocks;
        std::deque<json> events;
        json account = json::object();
        json estimate = json::object();
        std::string lastEquityMinute;
        std::chrono::steady_clock::time_point lastSave;
};
