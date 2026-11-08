#pragma once

#include <cmath>
#include "RiskEngine.hpp"
#include "../util/Format.hpp"
#include "../util/MarketTime.hpp"

/**
 * VOLATILITY_TARGET - each position should move the account by about the same amount on a typical day
 *
 *   "risk": {"name": "VOLATILITY_TARGET", "daily_volatility_percent": 0.25, "volatility_bars": 60}
 *
 *   daily volatility of the stock = standard deviation of the last volatility_bars bar returns x sqrt(bars per day)
 *   shares = equity x daily_volatility_percent% / (price x daily volatility)
 *
 * Calm stocks get bigger positions and jumpy ones smaller. Bars per day come from "bars_per_year" (minute bars
 * unless set, e.g. 252 for daily bars). Not traded until volatility_bars bars have been seen.
 */
class VolatilityTargetRisk : public RiskEngine
{
    public:
        static constexpr const char* NAME = "VOLATILITY_TARGET";
        static constexpr double DEFAULT_DAILY_VOLATILITY_PERCENT = 0.25;
        static constexpr int DEFAULT_VOLATILITY_BARS = 60;
        static constexpr double DEFAULT_BARS_PER_YEAR = MarketTime::TRADING_DAYS_PER_YEAR * MarketTime::MINUTES_PER_SESSION;

        explicit VolatilityTargetRisk(const json& settings)
            : RiskEngine(settings),
              dailyVolatilityPercent(settings.value("daily_volatility_percent", DEFAULT_DAILY_VOLATILITY_PERCENT)),
              volatilityBars(settings.value("volatility_bars", DEFAULT_VOLATILITY_BARS)),
              barsPerDay(settings.value("bars_per_year", DEFAULT_BARS_PER_YEAR) / MarketTime::TRADING_DAYS_PER_YEAR) {}

        std::string describe() const override
        {
            return std::string(NAME) + " " + Format::number(dailyVolatilityPercent, 2) + "%/day";
        }

        // Standard deviation of the last `count` bar-to-bar returns (0 if there aren't enough bars)
        static double barVolatility(const MarketData& data, int count)
        {
            const auto& bars = data.getData();
            if (count < 2 || bars.size() < static_cast<size_t>(count) + 1)
                return 0;
            double sum = 0, sumSquares = 0;
            for (size_t i = bars.size() - count; i < bars.size(); i++)
            {
                double change = bars[i].Close / bars[i - 1].Close - 1.0;
                sum += change;
                sumSquares += change * change;
            }
            double mean = sum / count;
            return std::sqrt(std::max(0.0, sumSquares / count - mean * mean));
        }

    protected:
        double targetShares(const Order& order, const SizingContext& context) const override
        {
            if (!context.bars)
                return 0;
            double dailyVolatility = barVolatility(*context.bars, volatilityBars) * std::sqrt(barsPerDay);
            if (dailyVolatility <= 0)
                return 0;
            return percentOf(context.equity, dailyVolatilityPercent) / (order.getPrice() * dailyVolatility);
        }

    private:
        double dailyVolatilityPercent;
        int volatilityBars;
        double barsPerDay;
};
