#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>
#include "PriceMatrix.hpp"
#include "../backtest/TradingCosts.hpp"
#include "../morning/MorningBacktest.hpp"

/**
 * PositionBook
 *
 * The trades a daily study holds: each trade is one or more legs (a pair is two: one long, one short), opened
 * and closed at a day's close, marked to every close in between. Every fill pays slippage, every sale the
 * regulatory fees; a closed trade is recorded on the line as one StockDay.
 */
class PositionBook
{
    public:
        using LegOrder = std::pair<size_t, int>;   // (stock, +1 long / -1 short)

        PositionBook(const PriceMatrix& prices, const TradingCosts& costs) : prices(prices), costs(costs) {}

        // Move every open leg to today's close (one with no bar today keeps its last); the P&L it made
        double mark(size_t day);

        // Open a trade at today's closes, `legValue` dollars a leg. -1 (nothing opened) if a price is missing
        // or a leg would be under one share. Adds its costs to `pnl` (as a loss).
        long open(const std::vector<LegOrder>& legs, double legValue, size_t day, double& pnl);

        // Close a trade at its legs' last marked prices; records it on `line`, adds the costs to `pnl`
        void close(long trade, MorningBacktest::Line& line, double& pnl);

        bool isOpen(long trade) const { return trades.count(trade) > 0; }
        size_t openTrades() const { return trades.size(); }

    private:
        struct Leg
        {
            size_t stock = 0;
            int side = 0;
            double shares = 0, entry = 0, last = 0, entryCosts = 0;
        };
        struct Trade
        {
            std::string openedOn;
            std::vector<Leg> legs;
        };

        double saleFees(double shares, double price) const;

        const PriceMatrix& prices;
        TradingCosts costs;
        std::map<long, Trade> trades;
        long next = 0;
};
