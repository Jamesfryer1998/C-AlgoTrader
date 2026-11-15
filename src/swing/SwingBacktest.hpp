#pragma once

#include <vector>
#include "SwingIndicators.hpp"
#include "SwingSettings.hpp"
#include "../backtest/TradingCosts.hpp"
#include "../morning/MorningBacktest.hpp"
#include "../pairs/PriceMatrix.hpp"

/**
 * SwingBacktest
 *
 * One swing variant traded day by day on daily bars, long only, at most `positions` held at once, each sized
 * leverage x equity / positions when it's chosen. Each day, in order:
 *
 *   at the open    sell what an exit rule picked yesterday, then buy what yesterday's signals picked
 *   in the day     a stop (breakouts) sells at the stop, or at the open if it opened under it
 *   at the close   mark everything; sell what's been held its days; pick tomorrow's exits and buys
 *
 * A variant "in at the signal's close" buys at the close the signal fires instead. Against it, the control:
 * as many buys as the signals actually made on each day, of random stocks that could have been bought, with
 * the same exits; or, for a variant limited to a few symbols, as many buys of each on random days.
 */
class SwingBacktest
{
    public:
        // Stocks to buy at each day's close, best first (index: day - first)
        using Signals = std::vector<std::vector<size_t>>;

        SwingBacktest(const SwingSettings& settings, const PriceMatrix& prices, const SwingIndicators& indicators,
                      const TradingCosts& costs);

        // Trade `variant` from day `first` to `last` (indices), on its own signals or the random control's
        void run(const SwingSettings::Variant& variant, bool chosen, size_t first, size_t last,
                 MorningBacktest::Line& line) const;

        // Each day's signals, best first
        Signals signals(const SwingSettings::Variant& variant, size_t first, size_t last) const;

        // The control's: for each day, random stocks to try in turn until it has bought as many as `bought`
        Signals randomSignals(const SwingSettings::Variant& variant, const Signals& bought, size_t first,
                              size_t last) const;

        // Trade the picks from day `first` to `last`, buying at most quota[i] on day i (no quotas: as many as
        // there are places). What was bought each day goes in `bought`.
        void trade(const SwingSettings::Variant& variant, const Signals& picks, const std::vector<size_t>* quotas,
                   size_t first, size_t last, MorningBacktest::Line& line, Signals& bought) const;

        static inline const std::string CHOSEN = "signal";

        // The last trading day of its week: the next falls on an earlier weekday (a new week), or there's none
        static bool lastOfWeek(const std::vector<std::string>& days, size_t day);
        static int weekday(const std::string& date);   // 0 Sunday ... 6 Saturday

    private:
        struct Position
        {
            size_t stock = 0;
            double shares = 0, entry = 0, last = 0, entryCosts = 0, stop = 0;
            int heldDays = 0;
            bool exitAtOpen = false;
            std::string openedOn;
        };
        struct Buy
        {
            size_t stock = 0;
            double value = 0;
            double stop = 0;   // as ATRs under the price, worked out at the fill (0: none)
        };

        // Could be bought on `day` and has what its exit rules need
        bool eligible(const SwingSettings::Variant& variant, size_t stock, size_t day) const;
        std::vector<size_t> candidates(const SwingSettings::Variant& variant, size_t day) const;
        int holdDays(const SwingSettings::Variant& variant) const;

        bool buy(const Buy& order, double price, size_t day, std::vector<Position>& book, double& pnl) const;
        void sell(Position& position, double price, MorningBacktest::Line& line, double& pnl) const;

        const SwingSettings& settings;
        const PriceMatrix& prices;
        const SwingIndicators& indicators;
        TradingCosts costs;
};
