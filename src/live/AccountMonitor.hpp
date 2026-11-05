#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "../alpaca/AlpacaBroker.hpp"
#include "../broker/AccountSnapshot.hpp"
#include "../broker/SimulatedBroker.hpp"
#include "../oms/OrderManagement.hpp"
#include "../oms/PerformanceTracker.hpp"

using json = nlohmann::json;

/**
 * What one stock holds right now, for display
 */
struct PositionSnapshot
{
    double quantity = 0;             // + long, - short, 0 flat
    double averagePrice = 0;
    double unrealisedPnL = 0;
    double unrealisedFraction = 0;   // unrealisedPnL as a fraction of the position's cost

    bool isOpen() const { return quantity != 0; }
    std::string describe() const;     // "1 @ 104.65", or "-" when flat
    std::string describePnL() const;  // "+$0.32 (+0.31%)", or "-" when flat
    json toJson() const;
};

/**
 * AccountMonitor
 *
 * Where the live app's table gets the account's value and each stock's position from. One per broker:
 * Alpaca's own figures when trading, the simulated broker's in a replay, our own estimate in a dry run.
 */
class AccountMonitor
{
    public:
        virtual ~AccountMonitor() = default;

        virtual AccountSnapshot account() = 0;
        virtual PositionSnapshot position(const std::string& ticker, double lastPrice) = 0;

        // Extra text for the table's header line, e.g. "REPLAY"
        virtual std::string headerNote() const { return ""; }
};

// Alpaca's real fills and prices
class AlpacaAccountMonitor : public AccountMonitor
{
    public:
        explicit AlpacaAccountMonitor(AlpacaBroker& broker) : broker(broker) {}
        AccountSnapshot account() override { return broker.getAccount(); }
        PositionSnapshot position(const std::string& ticker, double lastPrice) override;

    private:
        AlpacaBroker& broker;
};

// The simulated broker in a replay. "Today" is measured from the start of the replayed day.
class SimulatedAccountMonitor : public AccountMonitor
{
    public:
        explicit SimulatedAccountMonitor(SimulatedBroker& broker) : broker(broker) {}
        AccountSnapshot account() override;
        PositionSnapshot position(const std::string& ticker, double lastPrice) override;
        std::string headerNote() const override { return "REPLAY"; }

        // Call with each bar's day; a new day resets "today"
        void onDay(const std::string& day);

    private:
        SimulatedBroker& broker;
        std::string currentDay;
        double dayStartEquity = 0;
};

// Dry run: no orders reach Alpaca, so positions and P&L come from our own records
class DryRunAccountMonitor : public AccountMonitor
{
    public:
        DryRunAccountMonitor(OrderManagement& oms, PerformanceTracker& tracker) : oms(oms), tracker(tracker) {}
        AccountSnapshot account() override { return AccountSnapshot(); }
        PositionSnapshot position(const std::string& ticker, double lastPrice) override;
        std::string headerNote() const override;

    private:
        OrderManagement& oms;
        PerformanceTracker& tracker;
};
