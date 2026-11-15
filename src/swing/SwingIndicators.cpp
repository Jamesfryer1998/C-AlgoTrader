#include "SwingIndicators.hpp"

#include <algorithm>
#include <cmath>
#include <deque>

namespace
{
    const double RSI_MAX = 100.0;

    // `window` values ending at `day` (or the day before) all have a bar
    struct Gaps
    {
        explicit Gaps(const std::vector<double>& values)
        {
            missing.resize(values.size() + 1, 0);
            for (size_t i = 0; i < values.size(); i++)
                missing[i + 1] = missing[i] + (values[i] > 0 ? 0 : 1);
        }

        // Every value in [from, to) has a bar
        bool none(size_t from, size_t to) const { return missing[to] - missing[from] == 0; }

        std::vector<size_t> missing;   // missing bars before each index
    };

    SwingIndicators::Series zeros(size_t size) { return SwingIndicators::Series(size, 0.0f); }
}

SwingIndicators::SwingIndicators(const PriceMatrix& prices, const SwingSettings& settings)
    : prices(prices), minPrice(settings.minPrice), minDollarVolume(settings.minDollarVolume)
{
    for (size_t stock = 0; stock < prices.stocks(); stock++)
    {
        const auto& close = prices.close[stock];
        trend.push_back(average(close, settings.trendDays));
        exitAverage.push_back(average(close, settings.exitAboveDays));
        rsi.push_back(wilderRsi(close, settings.rsiDays));
        atr.push_back(wilderAtr(prices.high[stock], prices.low[stock], close, settings.atrDays));
        priorHigh.push_back(priorMax(close, settings.highDays - 1));
        priorVolume.push_back(priorAverage(prices.volume[stock], settings.volumeDays));
        dollarVolume.push_back(average(prices.dollarVolume[stock], settings.dollarVolumeDays));
        pastReturn.push_back(change(close, settings.returnDays));
    }
}

bool
SwingIndicators::tradable(size_t stock, size_t day) const
{
    return prices.close[stock][day] > minPrice && dollarVolume[stock][day] >= minDollarVolume;
}

SwingIndicators::Series
SwingIndicators::average(const std::vector<double>& values, int days)
{
    Series result = zeros(values.size());
    if (days <= 0)
        return result;
    Gaps gaps(values);
    double sum = 0;
    for (size_t day = 0; day < values.size(); day++)
    {
        sum += values[day];
        if (day >= static_cast<size_t>(days))
            sum -= values[day - days];
        if (day + 1 >= static_cast<size_t>(days) && gaps.none(day + 1 - days, day + 1))
            result[day] = static_cast<float>(sum / days);
    }
    return result;
}

SwingIndicators::Series
SwingIndicators::priorAverage(const std::vector<double>& values, int days)
{
    Series today = average(values, days);
    Series result = zeros(values.size());
    for (size_t day = 1; day < values.size(); day++)
        result[day] = today[day - 1];
    return result;
}

SwingIndicators::Series
SwingIndicators::priorMax(const std::vector<double>& values, int days)
{
    Series result = zeros(values.size());
    if (days <= 0)
        return result;
    Gaps gaps(values);
    std::deque<size_t> highest;   // indices whose values fall, the window's highest first
    for (size_t day = 0; day < values.size(); day++)
    {
        // The window is the `days` before today: [day - days, day)
        if (day >= static_cast<size_t>(days) && gaps.none(day - days, day))
        {
            while (!highest.empty() && highest.front() < day - days)
                highest.pop_front();
            result[day] = static_cast<float>(values[highest.front()]);
        }
        while (!highest.empty() && values[highest.back()] <= values[day])
            highest.pop_back();
        highest.push_back(day);
    }
    return result;
}

SwingIndicators::Series
SwingIndicators::wilderRsi(const std::vector<double>& closes, int days)
{
    Series result = zeros(closes.size());
    if (days <= 0)
        return result;
    double gain = 0, loss = 0;
    int seen = 0;   // changes since the last missing bar
    for (size_t day = 1; day < closes.size(); day++)
    {
        if (closes[day] <= 0 || closes[day - 1] <= 0)
        {
            seen = 0;
            gain = loss = 0;
            continue;
        }
        double move = closes[day] - closes[day - 1];
        double up = std::max(move, 0.0), down = std::max(-move, 0.0);
        seen++;
        if (seen <= days)   // the first `days` changes: a plain average
        {
            gain += up / days;
            loss += down / days;
        }
        else
        {
            gain = (gain * (days - 1) + up) / days;
            loss = (loss * (days - 1) + down) / days;
        }
        if (seen >= days)
            result[day] = static_cast<float>(loss > 0 ? RSI_MAX - RSI_MAX / (1 + gain / loss) : RSI_MAX);
    }
    return result;
}

SwingIndicators::Series
SwingIndicators::wilderAtr(const std::vector<double>& highs, const std::vector<double>& lows,
                           const std::vector<double>& closes, int days)
{
    Series result = zeros(closes.size());
    if (days <= 0)
        return result;
    double range = 0;
    int seen = 0;
    for (size_t day = 1; day < closes.size(); day++)
    {
        if (closes[day] <= 0 || closes[day - 1] <= 0)
        {
            seen = 0;
            range = 0;
            continue;
        }
        double trueRange = std::max({highs[day] - lows[day], std::fabs(highs[day] - closes[day - 1]),
                                     std::fabs(lows[day] - closes[day - 1])});
        seen++;
        range = seen <= days ? range + trueRange / days : (range * (days - 1) + trueRange) / days;
        if (seen >= days)
            result[day] = static_cast<float>(range);
    }
    return result;
}

SwingIndicators::Series
SwingIndicators::change(const std::vector<double>& closes, int days)
{
    Series result = zeros(closes.size());
    for (size_t day = static_cast<size_t>(std::max(days, 0)); days > 0 && day < closes.size(); day++)
        if (closes[day] > 0 && closes[day - days] > 0)
            result[day] = static_cast<float>(closes[day] / closes[day - days] - 1);
    return result;
}
