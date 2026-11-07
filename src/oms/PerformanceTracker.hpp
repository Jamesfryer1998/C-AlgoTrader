#pragma once

#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../util/Defaults.hpp"
#include "../util/MarketTime.hpp"

using json = nlohmann::json;

/**
 * PerformanceSummary
 *
 * A snapshot of how the account is doing. All money values are in dollars.
 */
struct PerformanceSummary
{
    double startingCapital = 0;
    double cash = 0;
    double equity = 0;              // cash + value of open positions at the latest price
    double realisedPnL = 0;         // locked in by closing (or reducing) positions, before commission
    double unrealisedPnL = 0;       // open positions marked at the latest price
    double commissionPaid = 0;
    double totalPnL = 0;            // equity - starting capital (realised + unrealised - commission)
    double roiPercent = 0;          // totalPnL / starting capital
    double maxDrawdownPercent = 0;  // biggest fall from a previous equity peak
    double sharpeRatio = 0;         // annualised, risk-free rate 0
    double annualisedReturnPercent = 0;
    int fills = 0;
    int closedTrades = 0;           // fills that closed or reduced a position
    int winningTrades = 0;
    int losingTrades = 0;           // break-even trades count as neither
    double winRatePercent = 0;      // winning / closed trades
    double avgWin = 0;
    double avgLoss = 0;
    double profitFactor = 0;        // gross profit / gross loss
};

/**
 * PerformanceTracker
 *
 * One place that works out P&L and risk metrics from fills and prices, used by both
 * the strategy tester, the backtest and live trading.
 *
 *   recordFill()  every time an order is filled (or, live, sent)
 *   markPrice()   with the latest price of a ticker
 *   recordEquity() once per bar, after marking prices, to build the equity curve
 *
 * Positions use average cost: buying more raises/lowers the average price, selling realises
 * (sell price - average price) x quantity. Shorts work the same way with the signs flipped.
 *
 * periodsPerYear is how many bars make a year, used to annualise Sharpe and returns.
 * 1-minute bars in regular hours: 252 days x 390 minutes.
 */
class PerformanceTracker
{
    public:
        static constexpr double MINUTE_BARS_PER_YEAR = MarketTime::TRADING_DAYS_PER_YEAR * MarketTime::MINUTES_PER_SESSION;

        PerformanceTracker(double startingCapital = Defaults::STARTING_CAPITAL, double periodsPerYear = MINUTE_BARS_PER_YEAR);

        void recordFill(const std::string& ticker, bool isBuy, double quantity, double price, double commission = 0.0);
        void markPrice(const std::string& ticker, double price);
        void recordEquity();

        double getEquity() const;
        double getPositionQuantity(const std::string& ticker) const;
        PerformanceSummary summary() const;

        static json toJson(const PerformanceSummary& summary);

    private:
        struct Holding
        {
            double quantity = 0;   // + long, - short
            double avgPrice = 0;
            double lastPrice = 0;
        };

        double startingCapital;
        double periodsPerYear;
        double cash;
        double realisedPnL = 0;
        double commissionPaid = 0;
        int fills = 0;

        std::map<std::string, Holding> holdings;
        std::vector<double> closedTradePnL;
        std::vector<double> equityCurve;
};
