#pragma once

#include <map>
#include <string>
#include <vector>
#include "../alpaca/AlpacaMultiBars.hpp"

/**
 * IntradayDay
 *
 * One regular session's 1-minute bars for a few hundred symbols, minute by minute from the open (0 = 09:30,
 * 389 = 15:59): each minute's open where it traded, and the last price so far (carried over quiet minutes).
 */
class IntradayDay
{
    public:
        IntradayDay() = default;

        // From Alpaca's bars (New York times); `rows`: the price matrix rows of the symbols wanted, `symbols` theirs
        IntradayDay(const AlpacaMultiBars::BarsBySymbol& bars, const std::vector<size_t>& rows,
                    const std::vector<std::string>& symbols);

        bool has(size_t row) const { return index.count(row) > 0; }

        // The last price at the close of `minute` (0 before its first trade)
        double price(size_t row, int minute) const;

        // What an order sent just before `minute` fills at: that minute's open, else the last price before it
        double fill(size_t row, int minute) const;

        static const int LAST_MINUTE;   // 15:59, the session's last bar

    private:
        std::map<size_t, size_t> index;
        std::vector<std::vector<double>> opens;
        std::vector<std::vector<double>> lasts;
};
