#pragma once

#include <ostream>
#include <string>
#include <vector>
#include "../backtest/Screener.hpp"

/**
 * ScreenReport
 *
 * What a screen found, in three parts:
 *   1. By strategy: how often each one could be tuned, made money on the test period and beat buy and hold
 *   2. Does picking by past results work? The best combinations on training against the average on test
 *   3. The combinations that held up in both periods
 */
class ScreenReport
{
    public:
        ScreenReport(const std::vector<Screener::Row>& rows, const std::vector<std::string>& strategies);

        void print(std::ostream& out) const;

        // Stock, strategy, Sharpe ratios, returns and settings, one line per combination
        static std::string combinationTable(const std::vector<Screener::Row>& rows, bool numbered = false);

    private:
        void printByStrategy(std::ostream& out) const;
        void printBestOnTraining(std::ostream& out) const;
        void printGoodInBothPeriods(std::ostream& out) const;

        const std::vector<Screener::Row>& rows;
        const std::vector<std::string>& strategies;
        int tradable = 0;   // combinations where some setting could be picked
};
