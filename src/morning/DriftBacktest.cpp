#include "DriftBacktest.hpp"

#include <cmath>

namespace
{
    // A position held for days
    struct Held
    {
        std::string symbol;
        std::string entryDay;
        int side = 0;
        double shares = 0;
        double entry = 0;          // the close it was bought or shorted at
        double last = 0;           // the last close it was marked to
        double entryCosts = 0;
        size_t exitIndex = 0;      // the calendar day it's sold at the close
    };

    const quant::DailyBar* barOn(const DriftBacktest::DailyBySymbol& daily, const std::string& symbol,
                                 const std::string& day)
    {
        auto stock = daily.find(symbol);
        if (stock == daily.end())
            return nullptr;
        auto bar = stock->second.find(day);
        return bar == stock->second.end() ? nullptr : &bar->second;
    }

    int signOf(double value)
    {
        return value > 0 ? 1 : value < 0 ? -1 : 0;
    }
}

void
DriftBacktest::run(const MorningSettings::Variant& variant, const PicksByDay& picks, const DailyBySymbol& daily,
                   const std::vector<std::string>& calendar, MorningBacktest::Line& line) const
{
    double slippage = costs.slippageFraction();
    std::vector<Held> held;
    for (size_t index = 0; index < calendar.size(); index++)
    {
        const std::string& day = calendar[index];
        double morning = line.equity, pnl = 0;

        // 1. Mark everything held to today's close (a stock with no bar today keeps yesterday's)
        for (auto& position : held)
            if (const quant::DailyBar* bar = barOn(daily, position.symbol, day); bar && bar->close > 0)
            {
                pnl += position.side * position.shares * (bar->close - position.last);
                position.last = bar->close;
            }

        // 2. Sell what's due at today's close
        for (auto position = held.begin(); position != held.end();)
        {
            if (position->exitIndex != index)
            {
                ++position;
                continue;
            }
            double exitCosts = position->shares * position->last * slippage
                             + (position->side > 0 ? costs.regulatoryFees.onSale(position->shares, position->last) : 0);
            pnl -= exitCosts;
            StockDay result;
            result.symbol = position->symbol;
            result.day = position->entryDay;
            result.trades = 1;
            result.grossPnl = position->side * position->shares * (position->last - position->entry);
            result.costs = position->entryCosts + exitCosts;
            result.netPnl = result.grossPnl - result.costs;
            result.wins = result.netPnl > 0 ? 1 : 0;
            line.stockDays.push_back(result);
            position = held.erase(position);
        }

        // 3. Buy or short today's picks at the close
        auto today = picks.find(day);
        if (today != picks.end())
        {
            double positionValue = settings.driftLeverage * morning / (variant.holdDays * settings.stocks);
            for (const auto& pick : today->second)
            {
                const quant::DailyBar* bar = barOn(daily, pick.symbol, day);
                if (!bar || bar->close <= 0)
                    continue;
                int side = variant.dayMove ? signOf(bar->close - bar->open) : pick.direction;
                if (variant.fade)
                    side = -side;
                double shares = std::floor(positionValue / bar->close);
                if (side == 0 || shares < 1)
                    continue;
                Held position;
                position.symbol = pick.symbol;
                position.entryDay = day;
                position.side = side;
                position.shares = shares;
                position.entry = position.last = bar->close;
                position.entryCosts = shares * bar->close * slippage
                                    + (side < 0 ? costs.regulatoryFees.onSale(shares, bar->close) : 0);
                position.exitIndex = index + variant.holdDays;
                pnl -= position.entryCosts;
                held.push_back(position);
            }
        }

        line.days.push_back(day);
        line.returns.push_back(morning > 0 ? pnl / morning : 0);
        line.equity += pnl;
    }
}
