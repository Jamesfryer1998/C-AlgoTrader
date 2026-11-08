#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "QuantOptions.hpp"

using json = nlohmann::json;

/**
 * QuantProjects
 *
 * Runs one of quant_app's projects: loads the stocks' daily closes (downloading them from Alpaca unless
 * offline), lines them up by date, then runs the project, prints its report and saves its CSVs in data/quant/.
 */
class QuantProjects
{
    public:
        QuantProjects(const json& algoConfig, const std::string& dataRoot);

        // 0 on success, 1 if the prices couldn't be loaded
        int run(const QuantOptions& options);

    private:
        void runRisk(const QuantOptions& options, const quant::PriceTable& prices) const;
        void runPairs(const QuantOptions& options, const quant::PriceTable& prices) const;
        void runScan(const quant::PriceTable& prices) const;
        void runCrossover(const QuantOptions& options, const quant::PriceTable& prices) const;

        json algoConfig;
        std::string dataRoot;
        std::string outputFolder;
};
