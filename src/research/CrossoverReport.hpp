#pragma once

#include <ostream>
#include <string>
#include "../quant/MovingAverageCrossover.hpp"

/**
 * CrossoverReport
 *
 * Prints quant_app macross' results: the crossover against buy and hold, with and without costs and
 * look-ahead, and a one-line verdict. Saves the daily averages, positions and returns as a CSV.
 */
class CrossoverReport
{
    public:
        CrossoverReport(const std::string& symbol, const quant::CrossoverSettings& settings,
                        const quant::CrossoverResult& result);

        void print(std::ostream& out) const;
        std::string save(const std::string& folder) const;   // returns the CSV's path

    private:
        std::string symbol;
        const quant::CrossoverSettings& settings;
        const quant::CrossoverResult& result;
};
