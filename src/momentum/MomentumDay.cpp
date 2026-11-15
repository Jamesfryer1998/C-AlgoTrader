#include "MomentumDay.hpp"

#include <algorithm>
#include <cmath>
#include "../util/MarketTime.hpp"

const int MomentumDay::MINUTES = MarketTime::MINUTES_PER_SESSION;

namespace
{
    const double TYPICAL_PARTS = 3.0;   // (high + low + close) / 3
}

MomentumDay::MomentumDay(const std::vector<IntradayBar>& bars)
{
    std::vector<double> opens(MINUTES, 0), closes(MINUTES, 0), traded(MINUTES, 0), shares(MINUTES, 0);
    for (const auto& bar : bars)
    {
        int minute = MarketTime::minuteOfDay(bar.time) - MarketTime::OPEN_MINUTE;
        if (minute < 0 || minute >= MINUTES || bar.close <= 0)
            continue;
        opens[minute] = bar.open;
        closes[minute] = bar.close;
        traded[minute] = (bar.high + bar.low + bar.close) / TYPICAL_PARTS * bar.volume;
        shares[minute] = bar.volume;
    }
    if (opens[0] <= 0)
        return;   // not usable
    double last = opens[0], weightedSoFar = 0, volumeSoFar = 0;
    for (int minute = 0; minute < MINUTES; minute++)
    {
        if (closes[minute] > 0)
            last = closes[minute];
        weightedSoFar += traded[minute];
        volumeSoFar += shares[minute];
        open.push_back(opens[minute] > 0 ? opens[minute] : last);
        close.push_back(last);
        weighted.push_back(weightedSoFar);
        volume.push_back(volumeSoFar);
    }
}

double
MomentumDay::priceAfter(int minute) const
{
    return close.empty() ? 0 : close[std::clamp(minute, 0, MINUTES - 1)];
}

double
MomentumDay::fillAt(int minute) const
{
    if (open.empty())
        return 0;
    return minute >= MINUTES ? close.back() : open[std::max(minute, 0)];
}

double
MomentumDay::vwapThrough(int minute) const
{
    if (volume.empty())
        return 0;
    int at = std::clamp(minute, 0, MINUTES - 1);
    return volume[at] > 0 ? weighted[at] / volume[at] : close[at];
}

std::vector<double>
MomentumDay::movesFromOpen() const
{
    std::vector<double> moves;
    for (double price : close)
        moves.push_back(std::fabs(price / open.front() - 1));
    return moves;
}
