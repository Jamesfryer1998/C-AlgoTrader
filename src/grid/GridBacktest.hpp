#pragma once

#include <random>
#include <string>
#include <vector>
#include "GridSettings.hpp"
#include "../morning/MorningBacktest.hpp"
#include "../pairs/PriceMatrix.hpp"

/**
 * GridBacktest
 *
 * One variant on a list of symbols, each with an equal sleeve of the account: start_invested of it bought at
 * the close before the first day, the rest kept as cash. After a close at least trigger_percent below the one
 * before, buy step_percent more of what the sleeve holds (as far as its cash goes); after a close that far
 * above, sell step_percent of it. The mirror does the opposite. Orders fill at the next open (or, at_close, at
 * that close), paying slippage_percent. A stock that stops trading is kept at its last price.
 *
 * Compared with:
 *   held      the same sleeves, never traded: the starting mix of stock and cash, left alone
 *   random    the same trigger days and sizes, each trade's direction by a coin
 */
class GridBacktest
{
    public:
        enum class Mode { Rule, Held, Random };

        GridBacktest(const GridSettings& settings, const PriceMatrix& prices);

        // `rows`: the symbols (price matrix rows); trades days `first` to `last`
        MorningBacktest::Line run(const GridSettings::Variant& variant, const std::vector<size_t>& rows, size_t first,
                                  size_t last, Mode mode, const std::string& selection) const;

        // The `count` most traded rows over the formation days before `day` that aren't funds or not_stocks
        std::vector<size_t> mostTraded(size_t day, size_t count) const;
        std::vector<size_t> fundRows() const;

    private:
        struct Sleeve
        {
            size_t row = 0;
            double shares = 0, cash = 0, last = 0;
            double pending = 0;   // shares to buy (+) or sell (-) at the next open
        };

        const GridSettings& settings;
        const PriceMatrix& prices;
};
