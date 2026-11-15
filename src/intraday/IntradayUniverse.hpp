#pragma once

#include <vector>
#include "IntradaySettings.hpp"
#include "../pairs/PriceMatrix.hpp"

/**
 * IntradayUniverse
 *
 * A month's stocks for intraday_app, picked from the formation_days of daily bars before it: the `stocks` with
 * the most dollar volume that are really stocks, each with the sector fund its daily returns moved with most,
 * its beta to that fund, and how much it usually strays from it in a day.
 *
 * "Really stocks": a bar every day, priced above min_price, not one of not_stocks, and moving less closely than
 * max_correlation with SPY, every fund and every stock already picked (which leaves out index and leveraged
 * funds and a second share class of the same company).
 */
namespace IntradayUniverse
{
    struct Member
    {
        size_t stock = 0;          // rows in the price matrix
        size_t fund = 0;
        double beta = 0;           // to its fund, daily
        double dailySigma = 0;     // the standard deviation of its daily return less beta x its fund's
    };

    // For the month starting at day index `monthStart`; empty if there isn't the history
    std::vector<Member> pick(const PriceMatrix& prices, const IntradaySettings& settings, size_t monthStart);

    double correlation(const std::vector<double>& a, const std::vector<double>& b);
    double beta(const std::vector<double>& stock, const std::vector<double>& fund);
    double residualSigma(const std::vector<double>& stock, const std::vector<double>& fund, double beta);
}
