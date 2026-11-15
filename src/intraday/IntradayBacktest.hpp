#pragma once

#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>
#include "IntradayDay.hpp"
#include "IntradaySettings.hpp"
#include "IntradayUniverse.hpp"
#include "../backtest/TradingCosts.hpp"
#include "../morning/MorningBacktest.hpp"
#include "../pairs/PriceMatrix.hpp"

/**
 * IntradayBacktest
 *
 * One variant over one day, flat at the open and at the close. Every step_minutes from first_minute:
 *
 *   exits     a stock whose gap to its sector has closed (measured from where the gap started, z back to
 *             exit_z), or that's been held hold_minutes
 *   entries   up to stocks_each_side a side: fading, buy the stocks furthest under their sector (z at most
 *             -entry_z) and short those furthest over; riding, the other way round. Biggest gaps first.
 *
 * z is the stock's move over the lookback less beta x its fund's, over its usual size for that long (its daily
 * residual sigma x sqrt(lookback / 390)). Each stock is lev x equity / (2 x stocks_each_side); hedged, the book
 * holds -beta x that in its fund, and each step trades only the funds' net change. Orders fill at the open of the minute
 * delay_minutes after the signal (with no delay, at the signal minute's close), paying slippage, and every sale
 * the regulatory fees. Whatever's open at 15:59 is sold at its close.
 *
 * The control: on each step, as many entries a side as the signals made that step, of random stocks from the
 * same month's list, each held for as many steps as the trade it stands in for, hedged the same way.
 */
class IntradayBacktest
{
    public:
        // What the signals entered: copied by the control
        struct Entry
        {
            size_t step = 0;
            int side = 0;            // +1 long, -1 short
            size_t exitStep = 0;
        };
        using Members = std::vector<IntradayUniverse::Member>;

        IntradayBacktest(const IntradaySettings& settings, const PriceMatrix& prices, const TradingCosts& costs);

        // One day's P&L (after costs) for a book worth `equity` that morning; each round trip goes on `line`.
        // Trading the signals, `entries` is filled with what they entered; as the control, it's copied.
        double day(const IntradaySettings::Variant& variant, const Members& members, const IntradayDay& bars,
                   const std::string& date, double equity, bool control, std::vector<Entry>& entries,
                   MorningBacktest::Line& line) const;

        // The minutes after the open it looks at, the last being the close
        std::vector<int> steps() const;

        // The gap of `member` from the close of minute `from` to the close of minute `to`, in z (its usual size
        // over `lookbackMinutes`); false if it can't be worked out
        static bool gapZ(const IntradayUniverse::Member& member, const IntradayDay& bars, int from, int to,
                         int lookbackMinutes, double& z);

        static inline const std::string CHOSEN = "signal";

    private:
        struct Position
        {
            size_t member = 0;
            int side = 0;
            double shares = 0, entry = 0, entryCosts = 0;
            double hedgeShares = 0;   // in its fund, signed
            size_t openedStep = 0, exitStep = 0;
            int gapFrom = 0;          // the minute its gap is measured from
            size_t record = 0;        // its entry in `entries`
        };
        struct FundBook
        {
            double shares = 0, cash = 0, costs = 0;
        };
        struct Order
        {
            size_t member = 0;
            int side = 0;
            size_t exitStep = 0;     // the control's: when the trade it copies came out
        };

        double tradeCosts(double shares, double price) const;   // shares signed: negative sells
        void moveFund(FundBook& fund, double shares, double price) const;
        // At a step: the signals' orders (best first), or the control's copies of what they entered
        std::vector<Order> signalOrders(const IntradaySettings::Variant& variant, const Members& members,
                                        const IntradayDay& bars, int minute, const std::set<size_t>& held) const;
        std::vector<Order> controlOrders(const Members& members, const IntradayDay& bars, int minute, size_t step,
                                         const std::set<size_t>& held, const std::vector<Entry>& entries,
                                         std::mt19937_64& random) const;

        // Where an order made at `minute` fills (see the class comment)
        double fillPrice(const IntradaySettings::Variant& variant, const IntradayDay& bars, size_t row, int minute,
                         bool atClose) const;

        // Sell (or buy back) at `price`; its hedge is added to `fundOrders` (fund row -> shares), traded net
        void sell(Position& position, double price, const IntradayUniverse::Member& member, const std::string& date,
                  std::map<size_t, double>& fundOrders, MorningBacktest::Line& line, double& pnl) const;

        const IntradaySettings& settings;
        const PriceMatrix& prices;
        TradingCosts costs;
};
