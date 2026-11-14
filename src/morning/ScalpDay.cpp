#include "ScalpDay.hpp"

#include "../backtest/StrategyTester.hpp"
#include "../data_access/MarketCondition.hpp"
#include "../util/Log.hpp"
#include "../util/MarketTime.hpp"

namespace
{
    const std::string MINUTE_INTERVAL = "1m";
    const double ALL_OF_IT = 100.0;   // percent: the stock's share of the account goes into each trade

    std::vector<MarketCondition> asMarketConditions(const std::string& symbol, const std::vector<IntradayBar>& bars)
    {
        std::vector<MarketCondition> converted;
        converted.reserve(bars.size());
        for (const auto& bar : bars)
            converted.emplace_back(bar.time, symbol, static_cast<float>(bar.open), static_cast<float>(bar.close),
                                   static_cast<int>(bar.volume), MINUTE_INTERVAL);
        return converted;
    }

    PerformanceSummary backtest(const json& config, const std::vector<MarketCondition>& bars, const TradingCosts& costs)
    {
        StrategyTester tester(config);
        tester.keepEquityCurve(false);
        tester.useCosts(costs);
        tester.runOn(bars);
        return tester.getSummary();
    }
}

ScalpDay::ScalpDay(const json& config, const json& strategy, const MorningSettings& settings, const TradingCosts& costs)
    : config(config), strategy(strategy), settings(settings), costs(costs)
{
}

json
ScalpDay::testerConfig(const std::string& symbol) const
{
    json tester = config;
    tester["ticker"] = symbol;
    tester["strategies"] = json::array({strategy});
    tester["risk"] = {{"name", "FIXED_FRACTION"}, {"position_percent", ALL_OF_IT},
                      {"max_stock_percent", ALL_OF_IT}, {"max_portfolio_percent", ALL_OF_IT}};
    return tester;
}

StockDay
ScalpDay::run(const StockSelection::Pick& pick, const std::vector<IntradayBar>& bars, double equity) const
{
    StockDay result;
    result.symbol = pick.symbol;
    result.day = bars.empty() ? "" : MarketTime::datePart(bars.front().time);
    if (bars.empty() || equity <= 0)
        return result;

    json tester = testerConfig(pick.symbol);
    std::vector<MarketCondition> converted = asMarketConditions(pick.symbol, bars);

    TradingCosts withCosts = costs;
    withCosts.startingCapital = settings.maxPositionValue(equity);
    TradingCosts free = withCosts;
    free.slippagePercent = 0;
    free.commissionPerTrade = 0;
    free.regulatoryFees = RegulatoryFees();

    bool verbose = Log::verbose;
    Log::verbose = false;   // thousands of stock-days: no per-order output
    PerformanceSummary net = backtest(tester, converted, withCosts);
    PerformanceSummary gross = backtest(tester, converted, free);
    Log::verbose = verbose;

    result.trades = net.closedTrades;
    result.wins = net.winningTrades;
    result.netPnl = net.totalPnL;
    result.grossPnl = gross.totalPnL;
    result.costs = gross.totalPnL - net.totalPnL;
    return result;
}
