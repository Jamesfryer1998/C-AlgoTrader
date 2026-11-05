#include "AccountMonitor.hpp"

#include <cmath>
#include <sstream>
#include "../alpaca/AlpacaApi.hpp"
#include "../util/Format.hpp"

namespace
{
    const std::string FLAT = "-";            // shown for a stock we don't hold
    const int PNL_PERCENT_DECIMALS = 2;      // minute moves are small
    const std::string REPLAY_NOTE = "REPLAY";
    const std::string DRY_RUN_NOTE = "dry run estimate ";

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

}

std::string
PositionSnapshot::describe() const
{
    if (!isOpen())
        return FLAT;
    std::ostringstream text;
    text << quantity << " @ " << Format::number(averagePrice);
    return text.str();
}

std::string
PositionSnapshot::describePnL() const
{
    if (!isOpen())
        return FLAT;
    return Format::dollarChange(unrealisedPnL) + " (" + Format::percentChange(unrealisedFraction, PNL_PERCENT_DECIMALS) + ")";
}

json
PositionSnapshot::toJson() const
{
    return {{AlpacaApi::QUANTITY, quantity}, {AlpacaApi::AVERAGE_PRICE, averagePrice}, {AlpacaApi::UNREALISED_PNL, unrealisedPnL}};
}

PositionSnapshot
AlpacaAccountMonitor::position(const std::string& ticker, double lastPrice)
{
    broker.getLatestPosition(ticker);   // fresh: the OMS skips its own check while an order is filling
    const json& details = broker.lastPositionDetails();
    if (details.empty())
        return PositionSnapshot();

    PositionSnapshot snapshot;
    snapshot.quantity = std::fabs(AlpacaApi::numberField(details, AlpacaApi::QUANTITY));
    if (details.value(AlpacaApi::SIDE, "") == AlpacaApi::SHORT)
        snapshot.quantity = -snapshot.quantity;
    snapshot.averagePrice = AlpacaApi::numberField(details, AlpacaApi::AVERAGE_PRICE);
    snapshot.unrealisedPnL = AlpacaApi::numberField(details, AlpacaApi::UNREALISED_PNL);
    snapshot.unrealisedFraction = AlpacaApi::numberField(details, AlpacaApi::UNREALISED_FRACTION);
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
SimulatedAccountMonitor::headerNote() const
{
    return REPLAY_NOTE;
}

std::string
DryRunAccountMonitor::headerNote() const
{
    return DRY_RUN_NOTE + Format::dollarChange(tracker.summary().totalPnL);
}
