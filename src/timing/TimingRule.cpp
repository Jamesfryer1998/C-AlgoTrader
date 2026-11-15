#include "TimingRule.hpp"

#include <algorithm>
#include <functional>
#include <sstream>

namespace
{
    const std::string TREND = "trend", CROSS = "cross", MOMENTUM = "momentum", DIP = "dip", BREAKOUT = "breakout",
                      BELOW_AVERAGE = "below_average", VOL_TARGET = "vol_target", VOL_FILTER = "vol_filter",
                      DRAWDOWN_STOP = "drawdown_stop", TURN_OF_MONTH = "turn_of_month",
                      TREND_AND_MOMENTUM = "trend_and_momentum";
    const std::string EMA = "ema", AND = "and";
    const double PERCENT = 100.0;
    const double IN = 1.0, OUT = 0.0;

    int whole(const json& parameters, const std::string& name) { return parameters.value(name, 0); }
    double fraction(const json& parameters, const std::string& name) { return parameters.value(name, 0.0) / PERCENT; }

    // "trend: sma 200, band 2%" - each parameter as it reads best
    std::string describe(const json& value)
    {
        if (value.is_string())
            return value.get<std::string>();
        std::ostringstream out;
        out << value.get<double>();
        return out.str();
    }

    // A rule that's in or out, flipping when `enter` or `leave` says so (each sees the day and whether it's in)
    std::vector<double> holding(size_t days, const std::function<bool(size_t)>& enter,
                                const std::function<bool(size_t)>& leave)
    {
        std::vector<double> targets(days, OUT);
        bool in = false;
        for (size_t day = 0; day < days; day++)
        {
            if (in && leave(day))
                in = false;
            else if (!in && enter(day))
                in = true;
            targets[day] = in ? IN : OUT;
        }
        return targets;
    }
}

const std::vector<std::string>&
TimingRule::familyNames()
{
    static const std::vector<std::string> names = {TREND, CROSS, MOMENTUM, DIP, BREAKOUT, BELOW_AVERAGE,
                                                   VOL_TARGET, VOL_FILTER, DRAWDOWN_STOP, TURN_OF_MONTH,
                                                   TREND_AND_MOMENTUM};
    return names;
}

std::string
TimingRule::name() const
{
    std::ostringstream out;
    out << family;
    bool first = true;
    for (const auto& [key, value] : parameters.items())
    {
        out << (first ? ": " : ", ") << key << " " << describe(value);
        first = false;
    }
    return out.str();
}

std::vector<TimingRule>
TimingRule::expand(const json& families)
{
    std::vector<TimingRule> rules;
    for (const auto& family : familyNames())
    {
        if (!families.contains(family))
            continue;
        std::vector<std::pair<std::string, json>> choices;   // parameter -> its values
        for (const auto& [parameter, values] : families[family].items())
            if (values.is_array() && !values.empty())
                choices.push_back({parameter, values});
        std::vector<size_t> at(choices.size(), 0);
        while (true)
        {
            TimingRule rule;
            rule.family = family;
            rule.parameters = json::object();
            for (size_t i = 0; i < choices.size(); i++)
                rule.parameters[choices[i].first] = choices[i].second[at[i]];
            bool possible = (family != CROSS || whole(rule.parameters, "fast") < whole(rule.parameters, "slow"))
                         && (family != BREAKOUT || whole(rule.parameters, "exit_days") < whole(rule.parameters, "enter_days"))
                         && (family != DIP || rule.parameters.value("exit_above", 0.0) > rule.parameters.value("enter_below", 0.0));
            if (possible)
                rules.push_back(rule);
            size_t i = 0;   // the next combination, like an odometer
            while (i < at.size() && ++at[i] == choices[i].second.size())
                at[i++] = 0;
            if (i == at.size())
                break;
        }
    }
    return rules;
}

std::vector<double>
TimingRule::targets(const TimingSeries& series) const
{
    const auto& p = parameters;
    const auto& close = series.close;
    size_t days = series.size();
    auto average = [&](int span) -> const TimingSeries::Series& {
        return p.value("average", std::string()) == EMA ? series.ema(span) : series.sma(span);
    };
    auto above = [&](const TimingSeries::Series& line, size_t day, double band) {
        return series.known(line, day) && close[day] > line[day] * (1 + band);
    };

    if (family == TREND)
    {
        const auto& line = average(whole(p, "days"));
        double band = fraction(p, "band_percent");
        return holding(days, [&](size_t d) { return above(line, d, band); },
                       [&](size_t d) { return !series.known(line, d) || close[d] < line[d] * (1 - band); });
    }
    if (family == CROSS)
    {
        const auto& fast = average(whole(p, "fast"));
        const auto& slow = average(whole(p, "slow"));
        std::vector<double> targets(days, OUT);
        for (size_t d = 0; d < days; d++)
            targets[d] = series.known(slow, d) && series.known(fast, d) && fast[d] > slow[d] ? IN : OUT;
        return targets;
    }
    if (family == MOMENTUM)
    {
        const auto& change = series.change(whole(p, "days"));
        double threshold = fraction(p, "above_percent");
        std::vector<double> targets(days, OUT);
        for (size_t d = 0; d < days; d++)
            targets[d] = series.known(change, d) && change[d] > threshold ? IN : OUT;
        return targets;
    }
    if (family == DIP)
    {
        int rsiDays = whole(p, "rsi_days");
        const auto& rsi = series.rsi(rsiDays);   // 0 is a real RSI (no gains at all), so known by its warm-up
        int trendDays = whole(p, "trend_days"), maxHold = whole(p, "max_hold_days");
        const TimingSeries::Series* trend = trendDays > 0 ? &series.sma(trendDays) : nullptr;
        double enter = p.value("enter_below", 0.0), leave = p.value("exit_above", 0.0);
        size_t entered = 0;
        return holding(days,
                       [&](size_t d) {
                           bool dipped = d >= static_cast<size_t>(rsiDays) && rsi[d] < enter && (!trend || above(*trend, d, 0));
                           if (dipped)
                               entered = d;
                           return dipped;
                       },
                       [&](size_t d) { return rsi[d] > leave || (maxHold > 0 && d - entered >= static_cast<size_t>(maxHold)); });
    }
    if (family == BREAKOUT)
    {
        const auto& high = series.priorHigh(whole(p, "enter_days"));
        const auto& low = series.priorLow(whole(p, "exit_days"));
        return holding(days, [&](size_t d) { return series.known(high, d) && close[d] > high[d]; },
                       [&](size_t d) { return series.known(low, d) && close[d] < low[d]; });
    }
    if (family == BELOW_AVERAGE)
    {
        const auto& line = series.sma(whole(p, "days"));
        double under = fraction(p, "below_percent");
        return holding(days, [&](size_t d) { return series.known(line, d) && close[d] < line[d] * (1 - under); },
                       [&](size_t d) { return above(line, d, 0); });
    }
    if (family == VOL_TARGET)
    {
        const auto& volatility = series.volatility(whole(p, "days"));
        int trendDays = whole(p, "trend_days");
        const TimingSeries::Series* trend = trendDays > 0 ? &series.sma(trendDays) : nullptr;
        double target = fraction(p, "target_percent");
        std::vector<double> targets(days, OUT);
        for (size_t d = 0; d < days; d++)
            if (series.known(volatility, d) && (!trend || above(*trend, d, 0)))
                targets[d] = std::min(IN, target / volatility[d]);
        return targets;
    }
    if (family == VOL_FILTER)
    {
        const auto& volatility = series.volatility(whole(p, "days"));
        double ceiling = fraction(p, "below_percent");
        std::vector<double> targets(days, OUT);
        for (size_t d = 0; d < days; d++)
            targets[d] = series.known(volatility, d) && volatility[d] < ceiling ? IN : OUT;
        return targets;
    }
    if (family == DRAWDOWN_STOP)
    {
        size_t highDays = static_cast<size_t>(whole(p, "high_days"));
        double drop = fraction(p, "drop_percent");
        const auto& reenter = series.sma(whole(p, "reenter_days"));
        size_t entered = 0;
        return holding(days,
                       [&](size_t d) {
                           bool back = above(reenter, d, 0);
                           if (back)
                               entered = d;
                           return back;
                       },
                       [&](size_t d) {
                           size_t from = std::max(entered, d >= highDays ? d - highDays : 0);
                           double highest = *std::max_element(close.begin() + from, close.begin() + d + 1);
                           return close[d] < highest * (1 - drop);
                       });
    }
    if (family == TURN_OF_MONTH)
    {
        int last = whole(p, "last_days"), first = whole(p, "first_days");
        std::vector<double> targets(days, OUT);
        for (size_t d = 0; d + 1 < days; d++)   // decided at today's close for tomorrow
            targets[d] = series.daysToMonthEnd(d + 1) <= last || series.dayOfMonth(d + 1) <= first ? IN : OUT;
        return targets;
    }
    if (family == TREND_AND_MOMENTUM)
    {
        const auto& line = series.sma(whole(p, "trend_days"));
        const auto& change = series.change(whole(p, "momentum_days"));
        bool both = p.value("join", AND) == AND;
        std::vector<double> targets(days, OUT);
        for (size_t d = 0; d < days; d++)
        {
            bool trending = above(line, d, 0), rising = series.known(change, d) && change[d] > 0;
            targets[d] = (both ? trending && rising : trending || rising) ? IN : OUT;
        }
        return targets;
    }
    return std::vector<double>(days, OUT);
}
