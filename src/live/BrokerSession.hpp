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
 * replay:
 *
 *   AlpacaSession   orders go to Alpaca (or are only printed in a dry run); figures are Alpaca's own
 *   ReplaySession   a simulated broker fills orders at each saved bar's close and runs stop losses itself
 *
 * Around each stock's decision LiveTrader calls beforeDecision(bar) and afterDecision(): a replay uses them to
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

        virtual void beforeDecision(const MarketCondition& bar) {}
        virtual void afterDecision() {}

        // Printed when the session ends
        virtual void printSummary(std::ostream& out, const std::vector<std::string>& tickers) = 0;
};

class AlpacaSession : public BrokerSession
{
    public:
        // oms and tracker: our own records, for a dry run's figures
        AlpacaSession(const json& config, OrderManagement& oms, PerformanceTracker& tracker);

        bool connect() override;
        BrokerBase& broker() override { return alpaca; }
        AccountMonitor& account() override { return *monitor; }
        std::string recordsFolder() const override;
        void printSummary(std::ostream& out, const std::vector<std::string>& tickers) override;

    private:
        AlpacaBroker alpaca;
        OrderManagement& oms;
        PerformanceTracker& tracker;
        std::unique_ptr<AccountMonitor> monitor;
};

class ReplaySession : public BrokerSession
{
    public:
        // firstDay: the first replayed day, which names the records folder (data/replay/<day>/)
        ReplaySession(const json& config, const std::string& firstDay);

        bool connect() override;
        BrokerBase& broker() override { return simulated; }
        AccountMonitor& account() override { return monitor; }
        std::string recordsFolder() const override;
        void beforeDecision(const MarketCondition& bar) override;
        void afterDecision() override;
        void printSummary(std::ostream& out, const std::vector<std::string>& tickers) override;

    private:
        json config;
        std::string firstDay;
        MarketData latestBars;          // what the simulated broker prices orders off
        SimulatedBroker simulated;
        SimulatedAccountMonitor monitor;
};
