#pragma once

#include <ostream>
#include <string>
#include <vector>
#include "../quant/PairsTrading.hpp"

/**
 * PairsReport
 *
 * Prints quant_app pairs' results: is the pair cointegrated (overall and year by year), how the z-score
 * strategy did, and its latest trades. Saves the daily z-scores / positions and the trades as CSVs.
 */
class PairsReport
{
    public:
        PairsReport(const quant::PriceTable& prices, const quant::PairsSettings& settings, const quant::PairsResult& result);

        void print(std::ostream& out) const;

        // Saves pairs_<Y>_<X>_daily.csv and _trades.csv in `folder`; returns their paths
        std::vector<std::string> save(const std::string& folder) const;

    private:
        void printCointegration(std::ostream& out) const;
        void printBacktest(std::ostream& out) const;
        void printRecentTrades(std::ostream& out) const;

        const quant::PriceTable& prices;
        const quant::PairsSettings& settings;
        const quant::PairsResult& result;
};

/**
 * PairScanReport
 *
 * Prints quant_app scan's results: the most cointegrated pairs first. Saves every pair as a CSV.
 */
class PairScanReport
{
    public:
        PairScanReport(const quant::PriceTable& prices, const std::vector<quant::PairScan>& rows);

        void print(std::ostream& out) const;
        std::string save(const std::string& folder) const;   // returns the CSV's path

    private:
        const quant::PriceTable& prices;
        const std::vector<quant::PairScan>& rows;
};
