#include "PositionBook.hpp"

#include <cmath>

double
PositionBook::saleFees(double shares, double price) const
{
    return costs.regulatoryFees.onSale(shares, price);
}

double
PositionBook::mark(size_t day)
{
    double pnl = 0;
    for (auto& [id, trade] : trades)
        for (auto& leg : trade.legs)
        {
            double close = prices.close[leg.stock][day];
            if (close <= 0)
                continue;
            pnl += leg.side * leg.shares * (close - leg.last);
            leg.last = close;
        }
    return pnl;
}

long
PositionBook::open(const std::vector<LegOrder>& legs, double legValue, size_t day, double& pnl)
{
    Trade trade;
    trade.openedOn = prices.days[day];
    for (const auto& [stock, side] : legs)
    {
        double price = prices.close[stock][day];
        if (price <= 0)
            return -1;
        double shares = std::floor(legValue / price);
        if (shares < 1)
            return -1;
        Leg leg;
        leg.stock = stock;
        leg.side = side;
        leg.shares = shares;
        leg.entry = leg.last = price;
        leg.entryCosts = shares * price * costs.slippageFraction() + (side < 0 ? saleFees(shares, price) : 0);
        trade.legs.push_back(leg);
    }
    for (const auto& leg : trade.legs)
        pnl -= leg.entryCosts;
    trades[next] = trade;
    return next++;
}

void
PositionBook::close(long id, MorningBacktest::Line& line, double& pnl)
{
    auto found = trades.find(id);
    if (found == trades.end())
        return;
    StockDay result;
    result.day = found->second.openedOn;
    result.trades = 1;
    for (const auto& leg : found->second.legs)
    {
        double exitCosts = leg.shares * leg.last * costs.slippageFraction() + (leg.side > 0 ? saleFees(leg.shares, leg.last) : 0);
        pnl -= exitCosts;
        result.symbol += (result.symbol.empty() ? "" : "/") + prices.symbols[leg.stock];
        result.grossPnl += leg.side * leg.shares * (leg.last - leg.entry);
        result.costs += leg.entryCosts + exitCosts;
    }
    result.netPnl = result.grossPnl - result.costs;
    result.wins = result.netPnl > 0 ? 1 : 0;
    line.stockDays.push_back(result);
    trades.erase(found);
}
