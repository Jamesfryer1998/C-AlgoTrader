#include "PerformanceTracker.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace
{
    const double PERCENT = 100.0;
}

PerformanceTracker::PerformanceTracker(double capital, double barsPerYear)
: startingCapital(capital),
  periodsPerYear(barsPerYear),
  cash(capital)
{
}

void
PerformanceTracker::recordFill(const std::string& ticker, bool isBuy, double quantity, double price, double commission)
{
    if (quantity <= 0)
        return;

    Holding& holding = holdings[ticker];
    double signedQty = isBuy ? quantity : -quantity;

    cash -= signedQty * price + commission;
    commissionPaid += commission;
    holding.lastPrice = price;
    fills++;

    bool sameDirection = holding.quantity == 0 || (holding.quantity > 0) == (signedQty > 0);
    if (sameDirection)
    {
        // Opening or adding: new average price weighted by size
        double newQty = std::fabs(holding.quantity) + quantity;
        holding.avgPrice = (std::fabs(holding.quantity) * holding.avgPrice + quantity * price) / newQty;
        holding.quantity += signedQty;
        return;
    }

    // Reducing, closing or flipping: realise P&L on the part that closes
    double closingQty = std::min(quantity, std::fabs(holding.quantity));
    double direction = holding.quantity > 0 ? 1.0 : -1.0;   // long gains when price rises
    double pnl = closingQty * (price - holding.avgPrice) * direction;
    realisedPnL += pnl;
    closedTradePnL.push_back(pnl);

    holding.quantity += (signedQty > 0 ? closingQty : -closingQty);
    double remaining = quantity - closingQty;

    if (remaining > 0)
    {
        // Flipped to the other side: the leftover opens a new position at this price
        holding.quantity = signedQty > 0 ? remaining : -remaining;
        holding.avgPrice = price;
    }
    else if (std::fabs(holding.quantity) < 1e-9)
    {
        holding.quantity = 0;
        holding.avgPrice = 0;
    }
}

void
PerformanceTracker::markPrice(const std::string& ticker, double price)
{
    auto it = holdings.find(ticker);
    if (it != holdings.end())
        it->second.lastPrice = price;
}

double
PerformanceTracker::getEquity() const
{
    double equity = cash;
    for (const auto& [ticker, holding] : holdings)
        equity += holding.quantity * holding.lastPrice;
    return equity;
}

double
PerformanceTracker::getPositionQuantity(const std::string& ticker) const
{
    auto it = holdings.find(ticker);
    return it == holdings.end() ? 0.0 : it->second.quantity;
}

void
PerformanceTracker::recordEquity()
{
    equityCurve.push_back(getEquity());
}

PerformanceSummary
PerformanceTracker::summary() const
{
    PerformanceSummary s;
    s.startingCapital = startingCapital;
    s.cash = cash;
    s.equity = getEquity();
    s.realisedPnL = realisedPnL;
    s.commissionPaid = commissionPaid;
    s.fills = fills;

    for (const auto& [ticker, holding] : holdings)
        s.unrealisedPnL += holding.quantity * (holding.lastPrice - holding.avgPrice);

    s.totalPnL = s.equity - startingCapital;
    s.roiPercent = startingCapital != 0 ? s.totalPnL / startingCapital * PERCENT : 0.0;

    // Trade statistics (each closing fill counts as one trade)
    double grossProfit = 0, grossLoss = 0;
    for (double pnl : closedTradePnL)
    {
        if (pnl > 0)      { s.winningTrades++; grossProfit += pnl; }
        else if (pnl < 0) { s.losingTrades++;  grossLoss -= pnl; }
        // exactly break-even counts as neither a win nor a loss
    }
    s.closedTrades = static_cast<int>(closedTradePnL.size());
    s.winRatePercent = s.closedTrades ? PERCENT * s.winningTrades / s.closedTrades : 0.0;
    s.avgWin = s.winningTrades ? grossProfit / s.winningTrades : 0.0;
    s.avgLoss = s.losingTrades ? -grossLoss / s.losingTrades : 0.0;
    s.profitFactor = grossLoss > 0 ? grossProfit / grossLoss : 0.0;

    // Equity curve: drawdown, Sharpe, annualised return
    double peak = startingCapital;
    std::vector<double> returns;
    double previous = startingCapital;
    for (double equity : equityCurve)
    {
        peak = std::max(peak, equity);
        if (peak > 0)
            s.maxDrawdownPercent = std::max(s.maxDrawdownPercent, (peak - equity) / peak * PERCENT);
        if (previous != 0)
            returns.push_back((equity - previous) / previous);
        previous = equity;
    }

    if (returns.size() >= 2)
    {
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        double variance = 0;
        for (double r : returns)
            variance += (r - mean) * (r - mean);
        double stdev = std::sqrt(variance / (returns.size() - 1));
        if (stdev > 0)
            s.sharpeRatio = mean / stdev * std::sqrt(periodsPerYear);
    }

    if (!equityCurve.empty() && startingCapital > 0 && s.equity > 0)
    {
        double years = equityCurve.size() / periodsPerYear;
        s.annualisedReturnPercent = (std::pow(s.equity / startingCapital, 1.0 / years) - 1.0) * PERCENT;
    }

    return s;
}

json
PerformanceTracker::toJson(const PerformanceSummary& s)
{
    return {
        {"starting_capital", s.startingCapital}, {"cash", s.cash}, {"equity", s.equity},
        {"realised_pnl", s.realisedPnL}, {"unrealised_pnl", s.unrealisedPnL},
        {"commission_paid", s.commissionPaid}, {"total_pnl", s.totalPnL}, {"roi_percent", s.roiPercent},
        {"max_drawdown_percent", s.maxDrawdownPercent}, {"sharpe_ratio", s.sharpeRatio},
        {"annualised_return_percent", s.annualisedReturnPercent},
        {"fills", s.fills}, {"closed_trades", s.closedTrades}, {"winning_trades", s.winningTrades},
        {"losing_trades", s.losingTrades}, {"win_rate_percent", s.winRatePercent},
        {"avg_win", s.avgWin}, {"avg_loss", s.avgLoss}, {"profit_factor", s.profitFactor}
    };
}
