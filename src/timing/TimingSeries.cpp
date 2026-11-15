#include "TimingSeries.hpp"

#include <cmath>
#include <map>
#include "../swing/SwingIndicators.hpp"

namespace
{
    const double TRADING_DAYS = 252;
    const size_t YEAR_MONTH = 7;   // "2026-03" of "2026-03-02"

    TimingSeries::Series widen(const SwingIndicators::Series& values)
    {
        return TimingSeries::Series(values.begin(), values.end());
    }
}

TimingSeries::TimingSeries(const std::vector<quant::DailyBar>& bars, const std::vector<quant::DailyBar>& cash)
{
    std::map<std::string, double> cashClose;
    for (const auto& bar : cash)
        cashClose[bar.date] = bar.close;
    double lastCash = 0;
    for (const auto& bar : bars)
    {
        days.push_back(bar.date);
        open.push_back(bar.open);
        close.push_back(bar.close);
        auto found = cashClose.find(bar.date);
        double today = found == cashClose.end() ? 0 : found->second;
        cashReturn.push_back(today > 0 && lastCash > 0 ? today / lastCash - 1 : 0);
        if (today > 0)
            lastCash = today;
    }
}

const TimingSeries::Series&
TimingSeries::sma(int days) const
{
    auto found = smas.find(days);
    if (found == smas.end())
        found = smas.emplace(days, widen(SwingIndicators::average(close, days))).first;
    return found->second;
}

const TimingSeries::Series&
TimingSeries::ema(int days) const
{
    auto found = emas.find(days);
    if (found != emas.end())
        return found->second;
    Series values(size(), 0);
    double weight = 2.0 / (days + 1), average = 0;
    for (size_t day = 0; days > 0 && day < size(); day++)
    {
        average = day == 0 ? close[day] : average + weight * (close[day] - average);
        if (day + 1 >= static_cast<size_t>(days))   // after a warm-up as long as its span
            values[day] = average;
    }
    return emas.emplace(days, values).first->second;
}

const TimingSeries::Series&
TimingSeries::rsi(int days) const
{
    auto found = rsis.find(days);
    if (found == rsis.end())
        found = rsis.emplace(days, widen(SwingIndicators::wilderRsi(close, days))).first;
    return found->second;
}

const TimingSeries::Series&
TimingSeries::volatility(int days) const
{
    auto found = volatilities.find(days);
    if (found != volatilities.end())
        return found->second;
    Series values(size(), 0);
    for (size_t day = static_cast<size_t>(days); days > 1 && day < size(); day++)
    {
        double sum = 0, squares = 0;
        for (size_t i = day + 1 - days; i <= day; i++)
        {
            double change = close[i] / close[i - 1] - 1;
            sum += change;
            squares += change * change;
        }
        double mean = sum / days;
        values[day] = std::sqrt(std::max(squares / days - mean * mean, 0.0) * days / (days - 1) * TRADING_DAYS);
    }
    return volatilities.emplace(days, values).first->second;
}

const TimingSeries::Series&
TimingSeries::change(int days) const
{
    auto found = changes.find(days);
    if (found == changes.end())
        found = changes.emplace(days, widen(SwingIndicators::change(close, days))).first;
    return found->second;
}

const TimingSeries::Series&
TimingSeries::priorHigh(int days) const
{
    auto found = highs.find(days);
    if (found == highs.end())
        found = highs.emplace(days, widen(SwingIndicators::priorMax(close, days))).first;
    return found->second;
}

const TimingSeries::Series&
TimingSeries::priorLow(int days) const
{
    auto found = lows.find(days);
    if (found != lows.end())
        return found->second;
    Series flipped;
    for (double value : close)
        flipped.push_back(value > 0 ? 1 / value : 0);   // the highest of 1 / close is the lowest close
    Series values = widen(SwingIndicators::priorMax(flipped, days));
    for (double& value : values)
        value = value > 0 ? 1 / value : 0;
    return lows.emplace(days, values).first->second;
}

int
TimingSeries::dayOfMonth(size_t day) const
{
    int count = 1;
    while (day >= static_cast<size_t>(count) && days[day - count].substr(0, YEAR_MONTH) == days[day].substr(0, YEAR_MONTH))
        count++;
    return count;
}

int
TimingSeries::daysToMonthEnd(size_t day) const
{
    int count = 1;
    while (day + count < size() && days[day + count].substr(0, YEAR_MONTH) == days[day].substr(0, YEAR_MONTH))
        count++;
    return count;
}
