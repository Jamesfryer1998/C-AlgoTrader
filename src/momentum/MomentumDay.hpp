#pragma once

#include <vector>
#include "../alpaca/AlpacaMultiBars.hpp"

/**
 * MomentumDay
 *
 * One symbol's regular session as 390 one-minute bars (0 = 09:30 to 389 = 15:59), quiet minutes filled with
 * the last price, and its VWAP so far.
 */
class MomentumDay
{
    public:
        MomentumDay() = default;
        explicit MomentumDay(const std::vector<IntradayBar>& bars);

        // A full enough day: traded at the open and at the close
        bool usable() const { return open.size() == static_cast<size_t>(MINUTES) && open.front() > 0 && close.back() > 0; }

        double openingPrice() const { return open.front(); }
        double closingPrice() const { return close.back(); }

        // The last price at the end of minute `minute`
        double priceAfter(int minute) const;

        // Where an order sent at the end of minute `minute - 1` fills: minute `minute`'s open (or the last price)
        double fillAt(int minute) const;

        // The volume-weighted average price of minutes 0 to `minute`
        double vwapThrough(int minute) const;

        // |price after each minute / the day's open - 1|
        std::vector<double> movesFromOpen() const;

        static const int MINUTES;

    private:
        std::vector<double> open, close, weighted, volume;   // weighted and volume: running totals
};
