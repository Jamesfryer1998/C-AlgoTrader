#pragma once

#include <string>
#include <vector>

/**
 * EquityStats
 *
 * The account's value through a session, minute by minute: the worst fall from a high (max drawdown) and the
 * Sharpe ratio of its daily returns. For comparing runs, e.g. one risk engine against another.
 */
class EquityStats
{
    public:
        // The account's value at a bar's time ("YYYY-MM-DD HH:MM:SS")
        void update(double equity, const std::string& dateTime);

        // Worst fall from a high, as a fraction (0.05 = 5%)
        double maxDrawdown() const { return worstDrawdown; }

        // Mean / standard deviation of daily returns x sqrt(trading days per year); 0 with under two days
        double sharpe() const;

        int days() const { return static_cast<int>(dayCloses.size()); }

    private:
        double highest = 0;
        double worstDrawdown = 0;
        std::string currentDay;
        double firstEquity = 0;
        std::vector<double> dayCloses;   // the last value seen each day
};
