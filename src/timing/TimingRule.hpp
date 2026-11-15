#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "TimingSeries.hpp"

using json = nlohmann::json;

/**
 * TimingRule
 *
 * One rule for how much of the money to hold in the timed symbol, decided at each close for the next day:
 * a family (below) with one value for each of its parameters. TimingRule::expand makes every combination of
 * the values config/timing.json lists for each family.
 *
 *   trend               in above the average (sma or ema) of `days`; a band either side holds the last call
 *   cross               in while the `fast` average is above the `slow`
 *   momentum            in while the return over `days` is above `above_percent`
 *   dip                 in when RSI(rsi_days) falls under `enter_below` (above the trend_days average, if
 *                       trend_days isn't 0); out above `exit_above` or after max_hold_days
 *   breakout            in at a close above the highest of the enter_days before; out under the lowest of the
 *                       exit_days before
 *   below_average       in when the close is below_percent under its `days` average; out back over it
 *   vol_target          target_percent / the annualised volatility over `days`, at most all of it (nothing
 *                       under the trend_days average, if trend_days isn't 0)
 *   vol_filter          in while the volatility over `days` is under below_percent
 *   drawdown_stop       out once it's drop_percent under its highest close (over high_days, or since it got
 *                       back in); back in over the reenter_days average
 *   turn_of_month       in for the last_days trading days of each month and the first_days of the next
 *   trend_and_momentum  the trend_days trend and the momentum_days momentum, both (`and`) or either (`or`)
 */
struct TimingRule
{
    std::string family;
    json parameters;   // name -> the value tried

    std::string name() const;

    // How much to hold from each day's close (0 to 1), from what was known at that close
    std::vector<double> targets(const TimingSeries& series) const;

    // Every combination of each family's values (unknown families and impossible combinations left out)
    static std::vector<TimingRule> expand(const json& families);

    static const std::vector<std::string>& familyNames();
};
