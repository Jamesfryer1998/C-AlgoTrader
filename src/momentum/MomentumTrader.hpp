#pragma once

#include <deque>
#include <random>
#include <string>
#include <vector>
#include "MomentumDay.hpp"
#include "MomentumSettings.hpp"
#include "../backtest/TradingCosts.hpp"
#include "../morning/MorningBacktest.hpp"

/**
 * MomentumTrader
 *
 * One variant on one symbol, a day at a time (it keeps the history its rule needs between days):
 *
 *   noise area      (Zarattini, Aziz and Barbon 2024) each minute's usual move from the open is its average
 *                   over the last lookback_days; the band is the open (or yesterday's close, whichever is
 *                   further) +/- band_multiplier times it. At every check (10:00, 10:30, ... 15:30): above the
 *                   band, long; below, short; a position is closed when the price is back through the band or,
 *                   with vwap_stop, the day's VWAP; everything is sold at the close.
 *   last half hour  (Gao, Han, Li and Zhou 2018) at 15:30, the way it moved from yesterday's close to 10:00,
 *                   held to the close
 *   overnight       bought at the close, sold at the next open
 *
 * Each is sized at equity x min(max_leverage, target daily volatility / the last lookback_days' volatility)
 * (or max_leverage with no target). Orders fill at the open of the minute after the check, paying
 * cost_per_share and slippage_percent on each fill and the regulatory fees on sales (short sales too).
 *
 * The control, replay: the same trades (same times, same size, same prices) each in a random direction.
 */
class MomentumTrader
{
    public:
        struct Trade
        {
            double entry = 0, exit = 0;
            int side = 0;            // +1 long, -1 short
            double leverage = 0;     // what it held as a multiple of the account when it opened
        };

        MomentumTrader(const MomentumSettings::Variant& variant, const std::string& symbol, const TradingCosts& costs);

        // Trade one day for an account worth `equity` that morning; the day's P&L. With `record`, each round trip
        // goes on `line` and in `trades` (otherwise it's a warm-up day: only the history is kept).
        double day(const MomentumDay& bars, const std::string& date, double equity, bool record,
                   MorningBacktest::Line& line, std::vector<Trade>& trades);

        // The control: `trades` again for an account worth `equity`, each in a random direction
        double replay(const std::vector<Trade>& trades, const std::string& date, double equity,
                      std::mt19937_64& random, MorningBacktest::Line& line) const;

        // The band's usual move for each minute: the average of the days' moves from their opens
        static std::vector<double> averageMoves(const std::deque<std::vector<double>>& days);
        static double volatility(const std::deque<double>& closes);   // of the daily returns between them

    private:
        bool warmedUp() const;
        double leverage() const;
        double fillCost(double shares, double price, bool sale) const;
        double roundTrip(const Trade& trade, double shares, const std::string& date, MorningBacktest::Line* line) const;

        // The day's round trips (prices and sizes; not yet costed)
        void noiseArea(const MomentumDay& bars, std::vector<Trade>& made) const;
        void lastHalfHour(const MomentumDay& bars, std::vector<Trade>& made) const;

        MomentumSettings::Variant variant;
        std::string symbol;
        TradingCosts costs;
        std::deque<std::vector<double>> moves;   // the last lookback_days days' moves from their opens
        std::deque<double> closes;               // the last lookback_days + 1 closes
        double previousClose = 0;
        Trade overnight;                         // bought at yesterday's close (side 0: none)
        double overnightShares = 0;
};
