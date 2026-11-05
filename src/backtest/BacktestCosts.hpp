#pragma once

#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * BacktestCosts
 *
 * The money side of a backtest, read from the optimiser / screener settings:
 *   "starting_capital"      cash at the start (default $100,000)
 *   "commission_per_trade"  $ per fill (default 0, Alpaca charges none)
 *   "slippage_percent"      % of the price lost on every fill (default 0.01%)
 */
struct BacktestCosts
{
    static constexpr double DEFAULT_STARTING_CAPITAL = 100000.0;
    static constexpr double DEFAULT_SLIPPAGE_PERCENT = 0.01;

    double startingCapital = DEFAULT_STARTING_CAPITAL;
    double commissionPerTrade = 0.0;
    double slippagePercent = DEFAULT_SLIPPAGE_PERCENT;

    BacktestCosts() = default;

    explicit BacktestCosts(const json& settings)
        : startingCapital(settings.value("starting_capital", DEFAULT_STARTING_CAPITAL)),
          commissionPerTrade(settings.value("commission_per_trade", 0.0)),
          slippagePercent(settings.value("slippage_percent", DEFAULT_SLIPPAGE_PERCENT))
    {
    }

    double slippageFraction() const { return slippagePercent / 100.0; }
};
