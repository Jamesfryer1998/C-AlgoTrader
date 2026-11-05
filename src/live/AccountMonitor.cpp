#include "AccountMonitor.hpp"

#include <cmath>
#include <sstream>
#include "../util/Format.hpp"

namespace
{
    // A position at the latest price: P&L in dollars and as a fraction of what it cost
    PositionSnapshot valuePosition(double quantity, double averagePrice, double lastPrice)
    {
        PositionSnapshot snapshot;
        snapshot.quantity = quantity;
        snapshot.averagePrice = averagePrice;
        snapshot.unrealisedPnL = (lastPrice - averagePrice) * quantity;
        double cost = std::fabs(averagePrice * quantity);
        snapshot.unrealisedFraction = cost > 0 ? snapshot.unrealisedPnL / cost : 0.0;
        return snapshot;
    }

    // Alpaca sends numbers as text, e.g. {"qty": "5"}
    double numberField(const json& body, const char* key)
    {
        try { return body.contains(key) && body[key].is_string() ? std::stod(body[key].get<std::string>()) : 0.0; }
        catch (const std::exception&) { return 0.0; }
    }
}

std::string
PositionSnapshot::describe() const
{
    if (!isOpen())
        return "-";
    std::ostringstream text;
    text << quantity << " @ " << Format::number(averagePrice);
    return text.str();
}

std::string
PositionSnapshot::describePnL() const
{
    if (!isOpen())
        return "-";
    return Format::dollarChange(unrealisedPnL) + " (" + Format::percentChange(unrealisedFraction, 2) + ")";
}

json
PositionSnapshot::toJson() const
{
    return {{"qty", quantity}, {"avg_entry_price", averagePrice}, {"unrealized_pl", unrealisedPnL}};
}

PositionSnapshot
AlpacaAccountMonitor::position(const std::string& ticker, double lastPrice)
{
    broker.getLatestPosition(ticker);   // fresh: the OMS skips its own check while an order is filling
    const json& details = broker.lastPositionDetails();
    if (details.empty())
        return PositionSnapshot();

    PositionSnapshot snapshot;
    snapshot.quantity = std::fabs(numberField(details, "qty"));
    if (details.value("side", "long") == "short")
        snapshot.quantity = -snapshot.quantity;
    snapshot.averagePrice = numberField(details, "avg_entry_price");
    snapshot.unrealisedPnL = numberField(details, "unrealized_pl");
    snapshot.unrealisedFraction = numberField(details, "unrealized_plpc");
    return snapshot;
}

void
SimulatedAccountMonitor::onDay(const std::string& day)
{
    if (day == currentDay)
        return;
    currentDay = day;
    dayStartEquity = broker.getCurrentEquity();
}

AccountSnapshot
SimulatedAccountMonitor::account()
{
    AccountSnapshot snapshot;
    snapshot.available = true;
    snapshot.equity = broker.getCurrentEquity();
    snapshot.lastEquity = dayStartEquity;
    snapshot.cash = broker.getCurrentCash();
    return snapshot;
}

PositionSnapshot
SimulatedAccountMonitor::position(const std::string& ticker, double lastPrice)
{
    Position held = broker.getLatestPosition(ticker);
    if (held.getQuantity() == 0)
        return PositionSnapshot();
    return valuePosition(held.getQuantity(), held.getAvgPrice(), lastPrice);
}

PositionSnapshot
DryRunAccountMonitor::position(const std::string& ticker, double lastPrice)
{
    for (const auto& held : oms.getPositions())
        if (held.getTicker() == ticker && held.getQuantity() != 0)
            return valuePosition(held.getQuantity(), held.getAvgPrice(), lastPrice);
    return PositionSnapshot();
}

std::string
DryRunAccountMonitor::headerNote() const
{
    return "dry run estimate " + Format::dollarChange(tracker.summary().totalPnL);
}
