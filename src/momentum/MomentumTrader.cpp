#include "MomentumTrader.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    const int LONG = 1, SHORT = -1, FLAT = 0;
    const int FIRST_HALF_HOUR = 30;                  // minutes after the open: 10:00
    const int LAST_HALF_HOUR = 360;                  // 15:30
    const double PERCENT = 100.0;
}

MomentumTrader::MomentumTrader(const MomentumSettings::Variant& variant, const std::string& symbol,
                               const TradingCosts& costs)
    : variant(variant), symbol(symbol), costs(costs)
{
}

std::vector<double>
MomentumTrader::averageMoves(const std::deque<std::vector<double>>& days)
{
    std::vector<double> average(MomentumDay::MINUTES, 0);
    for (const auto& day : days)
        for (int minute = 0; minute < MomentumDay::MINUTES; minute++)
            average[minute] += day[minute] / days.size();
    return average;
}

double
MomentumTrader::volatility(const std::deque<double>& closes)
{
    if (closes.size() < 3)
        return 0;
    std::vector<double> returns;
    for (size_t i = 1; i < closes.size(); i++)
        returns.push_back(closes[i] / closes[i - 1] - 1);
    double mean = 0, squares = 0;
    for (double r : returns)
        mean += r / returns.size();
    for (double r : returns)
        squares += (r - mean) * (r - mean);
    return std::sqrt(squares / (returns.size() - 1));
}

bool
MomentumTrader::warmedUp() const
{
    return previousClose > 0 && moves.size() >= static_cast<size_t>(variant.lookbackDays)
        && closes.size() > static_cast<size_t>(variant.lookbackDays);
}

double
MomentumTrader::leverage() const
{
    if (variant.targetVolatility <= 0)
        return variant.maxLeverage;
    double recent = volatility(closes);
    return recent > 0 ? std::min(variant.maxLeverage, variant.targetVolatility / recent) : 0;
}

double
MomentumTrader::fillCost(double shares, double price, bool sale) const
{
    return shares * variant.costPerShare + shares * price * variant.slippagePercent / PERCENT
         + (sale ? costs.regulatoryFees.onSale(shares, price) : 0);
}

double
MomentumTrader::roundTrip(const Trade& trade, double shares, const std::string& date, MorningBacktest::Line* line) const
{
    // A long sells on the way out; a short sells on the way in
    double fees = fillCost(shares, trade.entry, trade.side == SHORT) + fillCost(shares, trade.exit, trade.side == LONG);
    double gross = trade.side * shares * (trade.exit - trade.entry);
    if (line)
    {
        StockDay result;
        result.symbol = symbol;
        result.day = date;
        result.trades = 1;
        result.grossPnl = gross;
        result.costs = fees;
        result.netPnl = gross - fees;
        result.wins = result.netPnl > 0 ? 1 : 0;
        line->stockDays.push_back(result);
    }
    return gross - fees;
}

void
MomentumTrader::noiseArea(const MomentumDay& bars, std::vector<Trade>& made) const
{
    std::vector<double> usual = averageMoves(moves);
    double top = std::max(bars.openingPrice(), previousClose), bottom = std::min(bars.openingPrice(), previousClose);
    double size = leverage();
    int position = FLAT;
    Trade open;
    for (int check = variant.checkMinutes; variant.checkMinutes > 0 && check < MomentumDay::MINUTES;
         check += variant.checkMinutes)
    {
        double price = bars.priceAfter(check - 1);
        double upper = top * (1 + variant.bandMultiplier * usual[check - 1]);
        double lower = bottom * (1 - variant.bandMultiplier * usual[check - 1]);
        double vwap = bars.vwapThrough(check - 1);
        bool out = (position == LONG && price < (variant.vwapStop ? std::max(upper, vwap) : upper))
                || (position == SHORT && price > (variant.vwapStop ? std::min(lower, vwap) : lower));
        if (out)
        {
            open.exit = bars.fillAt(check);
            made.push_back(open);
            position = FLAT;
        }
        if (position == FLAT && (price > upper || price < lower))
        {
            position = price > upper ? LONG : SHORT;
            open = Trade{bars.fillAt(check), 0, position, size};
        }
    }
    if (position != FLAT)
    {
        open.exit = bars.closingPrice();
        made.push_back(open);
    }
}

void
MomentumTrader::lastHalfHour(const MomentumDay& bars, std::vector<Trade>& made) const
{
    double firstHalfHour = bars.priceAfter(FIRST_HALF_HOUR - 1) / previousClose - 1;
    if (firstHalfHour != 0)
        made.push_back(Trade{bars.fillAt(LAST_HALF_HOUR), bars.closingPrice(), firstHalfHour > 0 ? LONG : SHORT, leverage()});
}

double
MomentumTrader::day(const MomentumDay& bars, const std::string& date, double equity, bool record,
                    MorningBacktest::Line& line, std::vector<Trade>& trades)
{
    trades.clear();
    if (!bars.usable())
        return 0;
    double pnl = 0;
    std::vector<Trade> made;

    // Overnight: sell what was bought at yesterday's close, at this open
    if (variant.kind == MomentumSettings::Kind::Overnight && overnight.side != FLAT)
    {
        overnight.exit = bars.openingPrice();
        if (record)
        {
            pnl += roundTrip(overnight, overnightShares, date, &line);
            trades.push_back(overnight);
        }
        overnight = Trade{};
    }

    if (warmedUp())
    {
        if (variant.kind == MomentumSettings::Kind::NoiseArea)
            noiseArea(bars, made);
        else if (variant.kind == MomentumSettings::Kind::LastHalfHour)
            lastHalfHour(bars, made);
        else if (record)
        {
            overnight = Trade{bars.closingPrice(), 0, LONG, leverage()};
            overnightShares = std::floor(equity * overnight.leverage / overnight.entry);
        }
    }
    for (const auto& trade : made)
    {
        double shares = std::floor(equity * trade.leverage / trade.entry);
        if (record && shares >= 1)
        {
            pnl += roundTrip(trade, shares, date, &line);
            trades.push_back(trade);
        }
    }

    moves.push_back(bars.movesFromOpen());
    while (moves.size() > static_cast<size_t>(variant.lookbackDays))
        moves.pop_front();
    closes.push_back(bars.closingPrice());
    while (closes.size() > static_cast<size_t>(variant.lookbackDays) + 1)
        closes.pop_front();
    previousClose = bars.closingPrice();
    return pnl;
}

double
MomentumTrader::replay(const std::vector<Trade>& trades, const std::string& date, double equity,
                       std::mt19937_64& random, MorningBacktest::Line& line) const
{
    std::bernoulli_distribution heads(0.5);
    double pnl = 0;
    for (Trade trade : trades)
    {
        trade.side = heads(random) ? LONG : SHORT;
        double shares = std::floor(equity * trade.leverage / trade.entry);
        if (shares >= 1)
            pnl += roundTrip(trade, shares, date, &line);
    }
    return pnl;
}
