#include "SwingBacktest.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <utility>
#include "../morning/StockSelection.hpp"

namespace
{
    const size_t YEAR_DIGITS = 4, MONTH_AT = 5, DAY_AT = 8, TWO_DIGITS = 2;
    const int MONTHS_BEFORE_MARCH = 3;
    const int SAKAMOTO[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};   // Sakamoto's day-of-week month offsets
    const int DAYS_A_WEEK = 7;
    const int LEAP_EVERY = 4, CENTURY = 100, LEAP_CENTURY = 400;
}

SwingBacktest::SwingBacktest(const SwingSettings& settings, const PriceMatrix& prices,
                             const SwingIndicators& indicators, const TradingCosts& costs)
    : settings(settings), prices(prices), indicators(indicators), costs(costs)
{
}

int
SwingBacktest::weekday(const std::string& date)
{
    int year = std::stoi(date.substr(0, YEAR_DIGITS));
    int month = std::stoi(date.substr(MONTH_AT, TWO_DIGITS));
    int day = std::stoi(date.substr(DAY_AT, TWO_DIGITS));
    if (month < MONTHS_BEFORE_MARCH)
        year--;
    return (year + year / LEAP_EVERY - year / CENTURY + year / LEAP_CENTURY + SAKAMOTO[month - 1] + day) % DAYS_A_WEEK;
}

bool
SwingBacktest::lastOfWeek(const std::vector<std::string>& days, size_t day)
{
    if (day + 1 >= days.size())
        return true;
    int today = weekday(days[day]), next = weekday(days[day + 1]);
    return next <= today;
}

int
SwingBacktest::holdDays(const SwingSettings::Variant& variant) const
{
    switch (variant.kind)
    {
        case SwingSettings::Kind::Breakout: return settings.breakoutHold;
        case SwingSettings::Kind::Losers: return settings.losersHold;
        default: return settings.pullbackMaxHold;
    }
}

bool
SwingBacktest::eligible(const SwingSettings::Variant& variant, size_t stock, size_t day) const
{
    if (!indicators.tradable(stock, day))
        return false;
    switch (variant.kind)
    {
        case SwingSettings::Kind::Pullback: return indicators.exitAverage[stock][day] > 0;
        case SwingSettings::Kind::Breakout: return indicators.atr[stock][day] > 0;
        default: return true;
    }
}

std::vector<size_t>
SwingBacktest::candidates(const SwingSettings::Variant& variant, size_t day) const
{
    std::vector<size_t> stocks;
    if (variant.symbols.empty())
    {
        for (size_t stock = 0; stock < prices.stocks(); stock++)
            if (eligible(variant, stock, day))
                stocks.push_back(stock);
        return stocks;
    }
    for (const auto& symbol : variant.symbols)
    {
        size_t stock = prices.row(symbol);
        if (stock < prices.stocks() && eligible(variant, stock, day))
            stocks.push_back(stock);
    }
    return stocks;
}

SwingBacktest::Signals
SwingBacktest::signals(const SwingSettings::Variant& variant, size_t first, size_t last) const
{
    Signals all;
    size_t most = variant.kind == SwingSettings::Kind::Losers ? settings.positionsOf(variant) : SIZE_MAX;
    for (size_t day = first; day <= last && day < prices.days.size(); day++)
    {
        std::vector<std::pair<double, size_t>> ranked;   // (lower is better, stock)
        bool weekEnd = lastOfWeek(prices.days, day);
        for (size_t stock : candidates(variant, day))
        {
            double close = prices.close[stock][day];
            if (variant.kind == SwingSettings::Kind::Pullback)
            {
                double trend = indicators.trend[stock][day], rsi = indicators.rsi[stock][day];
                if (trend > 0 && close > trend && rsi > 0 && rsi < settings.rsiBelow)
                    ranked.push_back({rsi, stock});
            }
            else if (variant.kind == SwingSettings::Kind::Breakout)
            {
                double high = indicators.priorHigh[stock][day], volume = indicators.priorVolume[stock][day];
                double ratio = volume > 0 ? prices.volume[stock][day] / volume : 0;
                if (high > 0 && close >= high && ratio >= settings.volumeRatio)
                    ranked.push_back({-ratio, stock});
            }
            else if (weekEnd && indicators.pastReturn[stock][day] < 0)
                ranked.push_back({indicators.pastReturn[stock][day], stock});
        }
        std::sort(ranked.begin(), ranked.end());
        std::vector<size_t> today;
        for (size_t i = 0; i < ranked.size() && i < most; i++)
            today.push_back(ranked[i].second);
        all.push_back(today);
    }
    return all;
}

SwingBacktest::Signals
SwingBacktest::randomSignals(const SwingSettings::Variant& variant, const Signals& bought, size_t first,
                             size_t last) const
{
    std::mt19937_64 random(StockSelection::stableHash(variant.name) ^ settings.randomSeed);
    Signals all(bought.size());
    if (variant.symbols.empty())   // random stocks on the same days
    {
        for (size_t i = 0; i < bought.size(); i++)
            if (!bought[i].empty())
            {
                all[i] = candidates(variant, first + i);
                std::shuffle(all[i].begin(), all[i].end(), random);
            }
        return all;
    }
    // A few symbols: each bought as often, on random days it could have been
    std::map<size_t, size_t> times;
    for (const auto& today : bought)
        for (size_t stock : today)
            times[stock]++;
    for (const auto& [stock, count] : times)
    {
        std::vector<size_t> days;
        for (size_t day = first; day <= last && day < prices.days.size(); day++)
            if (eligible(variant, stock, day))
                days.push_back(day - first);
        std::shuffle(days.begin(), days.end(), random);
        for (size_t i = 0; i < count && i < days.size(); i++)
            all[days[i]].push_back(stock);
    }
    return all;
}

bool
SwingBacktest::buy(const Buy& order, double price, size_t day, std::vector<Position>& book, double& pnl) const
{
    if (price <= 0)
        return false;
    double shares = std::floor(order.value / price);
    if (shares < 1)
        return false;
    Position position;
    position.stock = order.stock;
    position.shares = shares;
    position.entry = position.last = price;
    position.entryCosts = shares * price * costs.slippageFraction();
    position.stop = order.stop > 0 ? price - order.stop : 0;
    position.openedOn = prices.days[day];
    pnl -= position.entryCosts;
    book.push_back(position);
    return true;
}

void
SwingBacktest::sell(Position& position, double price, MorningBacktest::Line& line, double& pnl) const
{
    pnl += position.shares * (price - position.last);
    position.last = price;
    double exitCosts = position.shares * price * costs.slippageFraction()
                     + costs.regulatoryFees.onSale(position.shares, price);
    pnl -= exitCosts;
    StockDay result;
    result.symbol = prices.symbols[position.stock];
    result.day = position.openedOn;
    result.trades = 1;
    result.grossPnl = position.shares * (price - position.entry);
    result.costs = position.entryCosts + exitCosts;
    result.netPnl = result.grossPnl - result.costs;
    result.wins = result.netPnl > 0 ? 1 : 0;
    line.stockDays.push_back(result);
    position.shares = 0;   // sold
}

void
SwingBacktest::run(const SwingSettings::Variant& variant, bool chosen, size_t first, size_t last,
                   MorningBacktest::Line& line) const
{
    Signals bought;
    if (chosen)
    {
        trade(variant, signals(variant, first, last), nullptr, first, last, line, bought);
        return;
    }
    MorningBacktest::Line signalLine;
    signalLine.equity = line.equity;
    trade(variant, signals(variant, first, last), nullptr, first, last, signalLine, bought);
    Signals random = randomSignals(variant, bought, first, last);
    std::vector<size_t> quotas;   // as many as the signals bought each day, or for a few symbols the random days'
    for (size_t i = 0; i < bought.size(); i++)
        quotas.push_back(variant.symbols.empty() ? bought[i].size() : random[i].size());
    Signals boughtAtRandom;
    trade(variant, random, &quotas, first, last, line, boughtAtRandom);
}

void
SwingBacktest::trade(const SwingSettings::Variant& variant, const Signals& picked, const std::vector<size_t>* quotas,
                     size_t first, size_t last, MorningBacktest::Line& line, Signals& bought) const
{
    bought.assign(picked.size(), {});
    size_t slots = settings.positionsOf(variant);
    int hold = holdDays(variant);
    bool atClose = variant.entry == SwingSettings::Entry::SignalClose;
    std::vector<Position> book;
    std::vector<Buy> buys;   // at the next open

    auto sold = [](const Position& position) { return position.shares <= 0; };
    for (size_t day = first; day <= last && day < prices.days.size(); day++)
    {
        double morning = line.equity;
        double pnl = 0;

        // At the open: yesterday's exits, then yesterday's buys
        for (auto& position : book)
            if (position.exitAtOpen && prices.open[position.stock][day] > 0)
                sell(position, prices.open[position.stock][day], line, pnl);
        book.erase(std::remove_if(book.begin(), book.end(), sold), book.end());
        for (const auto& order : buys)
            if (book.size() < slots)
                buy(order, prices.open[order.stock][day], day, book, pnl);
        buys.clear();

        // In the day: stops
        for (auto& position : book)
        {
            double low = prices.low[position.stock][day], open = prices.open[position.stock][day];
            if (position.stop > 0 && low > 0 && low <= position.stop)
                sell(position, open > 0 ? std::min(open, position.stop) : position.stop, line, pnl);
        }
        book.erase(std::remove_if(book.begin(), book.end(), sold), book.end());

        // At the close: mark, then the time's-up and signal exits
        bool lastDay = day == last || day + 1 >= prices.days.size();
        for (auto& position : book)
        {
            double close = prices.close[position.stock][day];
            if (close > 0)
            {
                pnl += position.shares * (close - position.last);
                position.last = close;
            }
            position.heldDays++;
            if (position.heldDays >= hold || lastDay)
                sell(position, position.last, line, pnl);
            else if (variant.kind == SwingSettings::Kind::Pullback && close > 0
                     && close > indicators.exitAverage[position.stock][day])
                position.exitAtOpen = true;
        }
        book.erase(std::remove_if(book.begin(), book.end(), sold), book.end());

        // ... and tomorrow's buys
        if (!lastDay)
        {
            std::set<size_t> held;
            size_t staying = 0;
            for (const auto& position : book)
            {
                held.insert(position.stock);
                staying += position.exitAtOpen ? 0 : 1;
            }
            double value = settings.leverage * morning / slots;
            size_t quota = quotas ? (*quotas)[day - first] : SIZE_MAX;
            for (size_t stock : picked[day - first])
            {
                if (bought[day - first].size() >= quota)
                    break;
                if (held.count(stock) || (atClose ? book.size() : staying + buys.size()) >= slots)
                    continue;
                double stop = variant.kind == SwingSettings::Kind::Breakout
                            ? settings.stopAtr * indicators.atr[stock][day] : 0;
                Buy order{stock, value, stop};
                if (atClose && !buy(order, prices.close[stock][day], day, book, pnl))
                    continue;
                if (!atClose)
                    buys.push_back(order);
                held.insert(stock);
                bought[day - first].push_back(stock);
            }
        }

        line.days.push_back(prices.days[day]);
        line.returns.push_back(morning > 0 ? pnl / morning : 0);
        line.equity += pnl;
    }
}
