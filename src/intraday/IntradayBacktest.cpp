#include "IntradayBacktest.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include "../morning/StockSelection.hpp"
#include "../util/MarketTime.hpp"

namespace
{
    const int SIDES = 2;
    const int LONG = 1;
}

IntradayBacktest::IntradayBacktest(const IntradaySettings& settings, const PriceMatrix& prices,
                                   const TradingCosts& costs)
    : settings(settings), prices(prices), costs(costs)
{
}

std::vector<int>
IntradayBacktest::steps() const
{
    std::vector<int> minutes;
    for (int minute = settings.firstMinute; settings.stepMinutes > 0 && minute < IntradayDay::LAST_MINUTE;
         minute += settings.stepMinutes)
        minutes.push_back(minute);
    minutes.push_back(IntradayDay::LAST_MINUTE);
    return minutes;
}

bool
IntradayBacktest::gapZ(const IntradayUniverse::Member& member, const IntradayDay& bars, int from, int to,
                       int lookbackMinutes, double& z)
{
    if (from < 0 || lookbackMinutes <= 0)
        return false;
    double stockNow = bars.price(member.stock, to), stockThen = bars.price(member.stock, from);
    double fundNow = bars.price(member.fund, to), fundThen = bars.price(member.fund, from);
    if (stockNow <= 0 || stockThen <= 0 || fundNow <= 0 || fundThen <= 0 || member.dailySigma <= 0)
        return false;
    double gap = stockNow / stockThen - 1 - member.beta * (fundNow / fundThen - 1);
    z = gap / (member.dailySigma * std::sqrt(static_cast<double>(lookbackMinutes) / MarketTime::MINUTES_PER_SESSION));
    return true;
}

double
IntradayBacktest::tradeCosts(double shares, double price) const
{
    double size = std::fabs(shares);
    return size * price * costs.slippageFraction() + (shares < 0 ? costs.regulatoryFees.onSale(size, price) : 0);
}

void
IntradayBacktest::moveFund(FundBook& fund, double shares, double price) const
{
    if (shares == 0 || price <= 0)
        return;
    fund.shares += shares;
    fund.cash -= shares * price;
    fund.costs += tradeCosts(shares, price);
}

double
IntradayBacktest::fillPrice(const IntradaySettings::Variant& variant, const IntradayDay& bars, size_t row,
                            int minute, bool atClose) const
{
    if (atClose || variant.delayMinutes <= 0)
        return bars.price(row, minute);
    return bars.fill(row, minute + variant.delayMinutes);
}

void
IntradayBacktest::sell(Position& position, double price, const IntradayUniverse::Member& member, const std::string& date,
                       std::map<size_t, double>& fundOrders, MorningBacktest::Line& line, double& pnl) const
{
    if (price <= 0)
        price = position.entry;
    double exitCosts = tradeCosts(-position.side * position.shares, price);
    StockDay trade;
    trade.symbol = prices.symbols[member.stock];
    trade.day = date;
    trade.trades = 1;
    trade.grossPnl = position.side * position.shares * (price - position.entry);
    trade.costs = position.entryCosts + exitCosts;
    trade.netPnl = trade.grossPnl - trade.costs;
    trade.wins = trade.netPnl > 0 ? 1 : 0;
    line.stockDays.push_back(trade);
    pnl += trade.grossPnl - exitCosts;   // the entry's costs were taken when it opened
    fundOrders[member.fund] -= position.hedgeShares;
    position.shares = 0;   // sold
}

std::vector<IntradayBacktest::Order>
IntradayBacktest::signalOrders(const IntradaySettings::Variant& variant, const Members& members,
                               const IntradayDay& bars, int minute, const std::set<size_t>& held) const
{
    int direction = variant.direction == IntradaySettings::Direction::Fade ? 1 : -1;
    std::vector<std::pair<double, Order>> ranked;   // (-|z|, order): the biggest gaps first
    for (size_t m = 0; m < members.size(); m++)
    {
        double z = 0;
        if (held.count(m) || !gapZ(members[m], bars, minute - variant.lookbackMinutes, minute, variant.lookbackMinutes, z)
            || std::fabs(z) < settings.entryZ)
            continue;
        Order order;
        order.member = m;
        order.side = z < 0 ? direction : -direction;   // fading buys what's under its sector
        ranked.push_back({-std::fabs(z), order});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Order> orders;
    for (const auto& [score, order] : ranked)
        orders.push_back(order);
    return orders;
}

std::vector<IntradayBacktest::Order>
IntradayBacktest::controlOrders(const Members& members, const IntradayDay& bars, int minute, size_t step,
                                const std::set<size_t>& held, const std::vector<Entry>& entries,
                                std::mt19937_64& random) const
{
    std::vector<size_t> pool;
    for (size_t m = 0; m < members.size(); m++)
        if (!held.count(m) && bars.price(members[m].stock, minute) > 0 && bars.price(members[m].fund, minute) > 0)
            pool.push_back(m);
    std::shuffle(pool.begin(), pool.end(), random);
    std::vector<Order> orders;
    for (const auto& entry : entries)
        if (entry.step == step && orders.size() < pool.size())
            orders.push_back({pool[orders.size()], entry.side, entry.exitStep});
    return orders;
}

double
IntradayBacktest::day(const IntradaySettings::Variant& variant, const Members& members, const IntradayDay& bars,
                      const std::string& date, double equity, bool control, std::vector<Entry>& entries,
                      MorningBacktest::Line& line) const
{
    std::vector<int> minutes = steps();
    size_t closeStep = minutes.size() - 1;
    int direction = variant.direction == IntradaySettings::Direction::Fade ? 1 : -1;
    double value = settings.leverage * equity / (SIDES * settings.stocksEachSide);
    std::mt19937_64 random(StockSelection::stableHash(variant.name + date) ^ settings.randomSeed);
    if (!control)
        entries.clear();

    std::vector<Position> book;
    std::map<size_t, FundBook> funds;
    double pnl = 0;
    auto sold = [](const Position& position) { return position.shares <= 0; };

    for (size_t step = 0; step < minutes.size(); step++)
    {
        int minute = minutes[step];
        bool atClose = step == closeStep;
        std::map<size_t, double> fundOrders;   // this step's hedges, net by fund
        auto tradeFunds = [&]() {
            for (const auto& [row, shares] : fundOrders)
                moveFund(funds[row], shares, fillPrice(variant, bars, row, minute, atClose));
        };

        // Exits
        for (auto& position : book)
        {
            const auto& member = members[position.member];
            bool out = atClose;
            if (control)
                out = out || step >= position.exitStep;
            else
            {
                double z = 0;
                bool timeUp = minute - minutes[position.openedStep] >= variant.holdMinutes;
                bool gapClosed = gapZ(member, bars, position.gapFrom, minute, variant.lookbackMinutes, z)
                              && direction * position.side * z >= settings.exitZ;
                out = out || timeUp || gapClosed;
            }
            if (!out)
                continue;
            if (!control)
                entries[position.record].exitStep = step;
            sell(position, fillPrice(variant, bars, member.stock, minute, atClose), member, date, fundOrders, line, pnl);
        }
        book.erase(std::remove_if(book.begin(), book.end(), sold), book.end());
        if (atClose || minute > settings.lastEntryMinute)
        {
            tradeFunds();
            continue;
        }

        // Entries, up to stocks_each_side a side
        std::set<size_t> held;
        size_t longs = 0, shorts = 0;
        for (const auto& position : book)
        {
            held.insert(position.member);
            (position.side == LONG ? longs : shorts)++;
        }
        std::vector<Order> orders = control ? controlOrders(members, bars, minute, step, held, entries, random)
                                            : signalOrders(variant, members, bars, minute, held);
        for (const auto& order : orders)
        {
            size_t& count = order.side == LONG ? longs : shorts;
            const auto& member = members[order.member];
            double price = fillPrice(variant, bars, member.stock, minute, false);
            double shares = price > 0 ? std::floor(value / price) : 0;
            if (count >= settings.stocksEachSide || shares < 1)
                continue;
            Position position;
            position.member = order.member;
            position.side = order.side;
            position.shares = shares;
            position.entry = price;
            position.entryCosts = tradeCosts(order.side * shares, price);
            position.openedStep = step;
            position.exitStep = order.exitStep;
            position.gapFrom = minute - variant.lookbackMinutes;
            pnl -= position.entryCosts;
            double fundPrice = fillPrice(variant, bars, member.fund, minute, false);
            if (variant.hedge && fundPrice > 0)
            {
                position.hedgeShares = -order.side * member.beta * shares * price / fundPrice;
                fundOrders[member.fund] += position.hedgeShares;
            }
            if (!control)
            {
                position.record = entries.size();
                entries.push_back({step, order.side, closeStep});
            }
            book.push_back(position);
            count++;
        }
        tradeFunds();
    }

    // The funds, flat at the close: their P&L is the hedge's
    for (auto& [row, fund] : funds)
    {
        moveFund(fund, -fund.shares, bars.price(row, IntradayDay::LAST_MINUTE));
        StockDay hedge;
        hedge.symbol = prices.symbols[row];
        hedge.day = date;
        hedge.grossPnl = fund.cash;
        hedge.costs = fund.costs;
        hedge.netPnl = fund.cash - fund.costs;
        line.stockDays.push_back(hedge);   // no trades of its own: it's part of the stocks'
        pnl += hedge.netPnl;
    }
    return pnl;
}
