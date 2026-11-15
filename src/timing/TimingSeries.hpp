#pragma once

#include <map>
#include <string>
#include <vector>
#include "../quant/DailyBars.hpp"

/**
 * TimingSeries
 *
 * The timed symbol's daily bars (and the cash fund's returns, on the same days), with the indicators the rules
 * look at, each worked out once however many rules ask for it. Every indicator is as of that day's close, and
 * 0 where there isn't the history yet.
 */
class TimingSeries
{
    public:
        using Series = std::vector<double>;

        // `cash` may be empty: idle money then earns nothing
        TimingSeries(const std::vector<quant::DailyBar>& bars, const std::vector<quant::DailyBar>& cash);

        size_t size() const { return days.size(); }

        std::vector<std::string> days;
        Series open, close;
        Series cashReturn;   // close to close (0 where unknown)

        const Series& sma(int days) const;
        const Series& ema(int days) const;
        const Series& rsi(int days) const;              // Wilder's, 0 to 100
        const Series& volatility(int days) const;       // annualised, of the daily returns over `days`
        const Series& change(int days) const;           // close / the close `days` before - 1
        const Series& priorHigh(int days) const;        // highest close of the `days` before today
        const Series& priorLow(int days) const;         // lowest close of the `days` before today
        bool known(const Series& indicator, size_t day) const { return indicator[day] != 0; }

        // The trading day of its month, counting from 1 at the start and from 1 at the end
        int dayOfMonth(size_t day) const;
        int daysToMonthEnd(size_t day) const;

    private:
        mutable std::map<int, Series> smas, emas, rsis, volatilities, changes, highs, lows;
};
