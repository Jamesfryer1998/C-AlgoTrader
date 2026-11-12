#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "AccountMonitor.hpp"
#include "../alpaca/AlpacaBroker.hpp"
#include "../broker/SimulatedBroker.hpp"
#include "../data_access/MarketData.hpp"
#include "../oms/OrderManagement.hpp"
#include "../oms/PerformanceTracker.hpp"

using json = nlohmann::json;

/**
 * BrokerSession
 *
 * Where the live app's orders go and how it sees the account, so LiveTrader runs the same way live and in a
 * backtest:
 *
 *   AlpacaSession   orders go to Alpaca (or are only printed in a dry run); figures are Alpaca's own
 *   BacktestSession   a simulated broker fills orders at each saved bar's close and runs stop losses itself
 *
 * Around each stock's decision LiveTrader calls beforeDecision(bar) and afterDecision(): a backtest uses them to
 * show the simulated broker the bar and then fill that bar's orders; Alpaca needs neither.
 */
class BrokerSession
{
    public:
        virtual ~BrokerSession() = default;

        // Check the broker can be used (keys, account). False with a message if not.
        virtual bool connect() = 0;

        virtual BrokerBase& broker() = 0;
        virtual AccountMonitor& account() = 0;

        // Where this session's trades, positions and performance are saved, under data/
        virtual std::string recordsFolder() const = 0;

        // For the start-up summary
        virtual std::string describeMode() const = 0;      // e.g. "LIVE on Alpaca's paper account"
        virtual std::string describeAccount() const = 0;   // e.g. "ACTIVE, cash $98,150.70, buying power ..."
        virtual std::string describeMarketHours() { return ""; }   // e.g. "closed, opens in 56m (...)"; live only

        // For the dashboard: {"known", "is_open", "next_open_utc", "next_close_utc"}; not known in a backtest
        virtual json marketClock() { return {{"known", false}}; }

        // Between orders: keep the connection to the broker open, so the next order doesn't wait to open one
        virtual void keepConnectionWarm() {}

        virtual void beforeDecision(const MarketCondition& bar) {}
        virtual void afterDecision() {}

        // Orders filled so far (a backtest counts them; Alpaca's aren't tracked here)
        virtual size_t fills() const { return 0; }

        // Printed when the session ends
        virtual void printSummary(std::ostream& out, const std::vector<std::string>& tickers) = 0;
};

class AlpacaSession : public BrokerSession
{
    public:
        // oms and tracker: our own records, for a dry run's figures. client: how it talks to Alpaca (none: a
        // real AlpacaClient; tests pass a fake)
        AlpacaSession(const json& config, OrderManagement& oms, PerformanceTracker& tracker,
                      std::shared_ptr<AlpacaClient> client = nullptr);

        bool connect() override;
        BrokerBase& broker() override { return alpaca; }
        AccountMonitor& account() override { return *monitor; }
        std::string recordsFolder() const override;
        std::string describeMode() const override;
        std::string describeAccount() const override;
        std::string describeMarketHours() override;
        json marketClock() override;
        void keepConnectionWarm() override { alpaca.keepConnectionWarm(); }
        void printSummary(std::ostream& out, const std::vector<std::string>& tickers) override;

    private:
        AlpacaBroker alpaca;
        OrderManagement& oms;
        PerformanceTracker& tracker;
        std::unique_ptr<AccountMonitor> monitor;
};

class BacktestSession : public BrokerSession
{
    public:
        // firstDay: the first backtested day, which names the records folder (data/backtest/<day>/)
        BacktestSession(const json& config, const std::string& firstDay);

        bool connect() override;
        BrokerBase& broker() override { return simulated; }
        AccountMonitor& account() override { return monitor; }
        std::string recordsFolder() const override;
        std::string describeMode() const override;
        std::string describeAccount() const override;
        void beforeDecision(const MarketCondition& bar) override;
        void afterDecision() override;
        size_t fills() const override { return simulated.getFilledOrders().size(); }
        void printSummary(std::ostream& out, const std::vector<std::string>& tickers) override;

        // Start holding a position (--positions), valued at `price`, the last close before the backtest
        void startWithPosition(const std::string& ticker, double quantity, double averagePrice, double price);

    private:
        json config;
        std::string firstDay;
        size_t carriedPositions = 0;
        MarketData latestBars;          // what the simulated broker prices orders off
        SimulatedBroker simulated;
        SimulatedAccountMonitor monitor;
};
