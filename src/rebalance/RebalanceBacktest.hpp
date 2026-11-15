#pragma once

#include <map>
#include <random>
#include <string>
#include <vector>
#include "RebalanceSettings.hpp"
#include "../morning/MorningBacktest.hpp"

/**
 * RebalanceBacktest
 *
 * Harvey, Mazzoleni and Melone, "The Unintended Consequences of Rebalancing" (NBER w33554, 2025): funds that
 * hold 60% stocks and 40% bonds sell what's grown and buy what's shrunk at the end of each month, and the
 * market moves ahead of them. The signal is a 60/40 portfolio's drift since the last month end: its stock
 * weight less 60%. Over the month's last window_days trading days, hold the other way to the drift (stocks
 * have run ahead: be short stocks against bonds); on the next month's first day, the opposite (the reversal).
 * Positions are set at a close and held to the next, as market-on-close orders would be.
 *
 * The control: the same positions with each month's direction decided by a coin.
 */
class RebalanceBacktest
{
    public:
        struct Traded
        {
            std::vector<std::string> days;
            std::vector<double> returns;   // after costs
            std::vector<int> changes;      // 1 on a day the position changed
            std::vector<double> costs;     // as a fraction of the account
        };

        // `days` and `stocks`: the stock fund's closes; `bonds`: each bond fund's closes on those days (0: none)
        RebalanceBacktest(const RebalanceSettings& settings, const std::vector<std::string>& days,
                          const std::vector<double>& stocks, const std::map<std::string, std::vector<double>>& bonds);

        // At each close: the stock weight of a 60/40 portfolio set at the last month end, less 60%
        std::vector<double> drift(const std::string& bond) const;

        // What to hold through each day (from the close before), +1 all long stocks (against bonds), -1 short
        std::vector<double> positions(const RebalanceSettings::Variant& variant) const;

        // The same, each month's direction by a coin (the window and its reversal day together)
        std::vector<double> coinFlipped(const std::vector<double>& positions, std::mt19937_64& random) const;

        Traded trade(const RebalanceSettings::Variant& variant, const std::vector<double>& positions, size_t first,
                     size_t last) const;

        MorningBacktest::Line line(const Traded& traded, const std::string& variant, const std::string& selection,
                                   const std::string& symbol) const;

        size_t firstOfMonth(size_t day) const;   // the index of the first trading day of `day`'s month
        int daysToMonthEnd(size_t day) const;    // 1 on the month's last trading day

    private:
        double sizeOf(const RebalanceSettings::Variant& variant, double drift) const;
        bool sameMonth(size_t a, size_t b) const;

        const RebalanceSettings& settings;
        std::vector<std::string> days;
        std::vector<double> stocks;
        std::map<std::string, std::vector<double>> bonds;
};
