#pragma once

#include <nlohmann/json.hpp>
#include "../util/Defaults.hpp"

using json = nlohmann::json;

/**
 * TradingCosts
 *
 * The money side of a backtest, read from the optimiser / screener settings:
 *   "starting_capital"      cash at the start (default $100,000)
 *   "commission_per_trade"  $ per fill (default 0, Alpaca charges none)
 *   "slippage_percent"      % of the price lost on every fill (default 0.01%)
 */
struct TradingCosts
{
    static constexpr double DEFAULT_SLIPPAGE_PERCENT = 0.01;

    double startingCapital = Defaults::STARTING_CAPITAL;
    double commissionPerTrade = 0.0;
    double slippagePercent = DEFAULT_SLIPPAGE_PERCENT;

    TradingCosts() = default;

    explicit TradingCosts(const json& settings)
        : startingCapital(settings.value("starting_capital", Defaults::STARTING_CAPITAL)),
          commissionPerTrade(settings.value("commission_per_trade", 0.0)),
          slippagePercent(settings.value("slippage_percent", DEFAULT_SLIPPAGE_PERCENT))
    {
    }

    double slippageFraction() const { return slippagePercent / 100.0; }
};
