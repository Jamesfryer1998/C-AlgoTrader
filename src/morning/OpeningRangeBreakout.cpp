#include "OpeningRangeBreakout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include "../util/MarketTime.hpp"

namespace
{
    const double PERCENT = 100.0;
    const int FILLS_PER_TRADE = 2;

    bool isTrading(const DayEnd& dayEnd, const IntradayBar& bar)
    {
        return dayEnd.phaseOfBar(bar.time) == DayEnd::Phase::Trading;
    }
}

void
OpeningRangeBreakout::exitAt(MorningTrade& trade, const IntradayBar& bar, double price, const std::string& reason) const
{
    double slippage = costs.slippageFraction();
    trade.exitTime = bar.time;
    trade.exitPrice = price * (1 - trade.side * slippage);
    trade.exitReason = reason;
    trade.grossPnl = trade.side * (price - trade.entryBeforeCosts) * trade.shares;

    double slipped = trade.shares * (std::fabs(trade.entryPrice - trade.entryBeforeCosts) + std::fabs(trade.exitPrice - price));
    double sold = trade.side > 0 ? trade.exitPrice : trade.entryPrice;   // a long sells at the exit, a short at the entry
    trade.costs = slipped + costs.regulatoryFees.onSale(trade.shares, sold) + FILLS_PER_TRADE * costs.commissionPerTrade;
    trade.netPnl = trade.grossPnl - trade.costs;
}

std::optional<MorningTrade>
OpeningRangeBreakout::trade(const StockSelection::Pick& pick, const std::vector<IntradayBar>& bars, double equity) const
{
    int side = pick.direction;
    if (side == 0 || pick.atr <= 0 || equity <= 0)
        return std::nullopt;

    // The opening range
    int rangeEnd = MarketTime::OPEN_MINUTE + settings.openingMinutes;
    double high = -std::numeric_limits<double>::infinity(), low = std::numeric_limits<double>::infinity();
    size_t first = bars.size();
    for (size_t i = 0; i < bars.size(); i++)
    {
        int minute = MarketTime::minuteOfDay(bars[i].time);
        if (minute < MarketTime::OPEN_MINUTE)
            continue;
        if (minute >= rangeEnd)
        {
            first = i;
            break;
        }
        high = std::max(high, bars[i].high);
        low = std::min(low, bars[i].low);
    }
    if (high < low || first == bars.size())
        return std::nullopt;
    double level = side > 0 ? high : low;
    double riskPerShare = settings.stopAtrFraction * pick.atr;

    // The breakout
    for (size_t i = first; i < bars.size(); i++)
    {
        if (!isTrading(dayEnd, bars[i]))
            return std::nullopt;   // too near the close to open anything
        bool through = side > 0 ? bars[i].high >= level : bars[i].low <= level;
        if (!through)
            continue;

        bool touch = variant.entryFill == MorningSettings::EntryFill::Touch;
        size_t entryBar = touch ? i : i + 1;
        if (entryBar >= bars.size() || !isTrading(dayEnd, bars[entryBar]))
            return std::nullopt;
        double open = bars[entryBar].open;
        double rawEntry = side > 0 ? std::max(level, open) : std::min(level, open);

        double byRisk = settings.riskPercent / PERCENT * equity / riskPerShare;
        double byValue = settings.maxPositionValue(equity) / rawEntry;
        double shares = std::floor(std::min(byRisk, byValue));
        if (shares < 1)
            return std::nullopt;

        MorningTrade trade;
        trade.symbol = pick.symbol;
        trade.day = MarketTime::datePart(bars[entryBar].time);
        trade.side = side;
        trade.entryTime = bars[entryBar].time;
        trade.entryBeforeCosts = rawEntry;
        trade.entryPrice = rawEntry * (1 + side * costs.slippageFraction());
        trade.stopPrice = rawEntry - side * riskPerShare;
        trade.shares = shares;
        trade.riskDollars = riskPerShare * shares;
        double target = variant.targetR > 0 ? rawEntry + side * variant.targetR * riskPerShare : 0;
        int entryMinute = MarketTime::minuteOfDay(trade.entryTime);

        // The exit
        for (size_t j = entryBar; j < bars.size(); j++)
        {
            const IntradayBar& bar = bars[j];
            bool orderUnknown = touch && j == entryBar;   // the entry bar's low may have come before the entry
            double worst = side > 0 ? (orderUnknown ? bar.close : bar.low) : (orderUnknown ? bar.close : bar.high);
            double best = side > 0 ? (orderUnknown ? bar.close : bar.high) : (orderUnknown ? bar.close : bar.low);

            if (side * (worst - trade.stopPrice) <= 0)
            {
                double fill = orderUnknown ? bar.close
                            : (side > 0 ? std::min(trade.stopPrice, bar.open) : std::max(trade.stopPrice, bar.open));
                exitAt(trade, bar, fill, STOP);
                return trade;
            }
            if (target > 0 && side * (best - target) >= 0)
            {
                double fill = orderUnknown ? bar.close : (side > 0 ? std::max(target, bar.open) : std::min(target, bar.open));
                exitAt(trade, bar, fill, TARGET);
                return trade;
            }
            int held = MarketTime::minuteOfDay(bar.time) + 1 - entryMinute;   // minutes, at the bar's end
            if (variant.holdMinutes > 0 && held >= variant.holdMinutes)
            {
                exitAt(trade, bar, bar.close, TIME);
                return trade;
            }
            if (dayEnd.phaseOfBar(bar.time) == DayEnd::Phase::Close)
            {
                exitAt(trade, bar, bar.close, CLOSE);
                return trade;
            }
        }
        exitAt(trade, bars.back(), bars.back().close, END);
        return trade;
    }
    return std::nullopt;
}
