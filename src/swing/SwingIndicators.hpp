#pragma once

#include <vector>
#include "SwingSettings.hpp"
#include "../pairs/PriceMatrix.hpp"

/**
 * SwingIndicators
 *
 * What the swing signals look at, for every stock on every day, worked out once from the price matrix and
 * shared by every variant. Each is as of that day's close and 0 where it can't be worked out (not enough
 * history, or a missing bar in its window).
 */
struct SwingIndicators
{
    using Series = std::vector<float>;

    SwingIndicators(const PriceMatrix& prices, const SwingSettings& settings);

    std::vector<Series> trend;            // average close over trend_days
    std::vector<Series> exitAverage;      // average close over exit_above_days
    std::vector<Series> rsi;              // RSI over rsi_days (Wilder), 0 to 100
    std::vector<Series> atr;              // average true range over atr_days (Wilder)
    std::vector<Series> priorHigh;        // the highest close of the high_days - 1 days before
    std::vector<Series> priorVolume;      // average volume of the volume_days days before
    std::vector<Series> dollarVolume;     // average dollar volume over dollar_volume_days, today included
    std::vector<Series> pastReturn;       // close / the close return_days before - 1 (0 if not known)

    // Priced above min_price and trading at least min_dollar_volume a day, at this close
    bool tradable(size_t stock, size_t day) const;

    // The building blocks, for any one stock's daily values (0 = no bar)
    static Series average(const std::vector<double>& values, int days);       // over the `days` to today
    static Series priorAverage(const std::vector<double>& values, int days);  // over the `days` before today
    static Series priorMax(const std::vector<double>& values, int days);      // over the `days` before today
    static Series wilderRsi(const std::vector<double>& closes, int days);
    static Series wilderAtr(const std::vector<double>& highs, const std::vector<double>& lows,
                            const std::vector<double>& closes, int days);
    static Series change(const std::vector<double>& closes, int days);

    private:
        const PriceMatrix& prices;
        double minPrice;
        double minDollarVolume;
};
