#pragma once

#include <ostream>
#include <string>
#include <vector>
#include "../quant/MonteCarloRisk.hpp"

/**
 * RiskReport
 *
 * Prints quant_app risk's results: each stock, how they move together, and what the simulated futures lose
 * in a bad period compared with what real history lost. Saves every simulated return to a CSV.
 */
class RiskReport
{
    public:
        RiskReport(const quant::PriceTable& prices, const quant::RiskSettings& settings, const quant::RiskResult& result);

        void print(std::ostream& out) const;

        // Saves risk_simulations.csv in `folder` and returns its path
        std::string save(const std::string& folder) const;

        // The correlation matrix with a row and column per name
        static std::string correlationTable(const std::vector<std::string>& names, const quant::Matrix& correlation);

    private:
        void printStocks(std::ostream& out) const;
        void printLosses(std::ostream& out) const;
        void printRealityCheck(std::ostream& out) const;

        const quant::PriceTable& prices;
        const quant::RiskSettings& settings;
        const quant::RiskResult& result;
};
