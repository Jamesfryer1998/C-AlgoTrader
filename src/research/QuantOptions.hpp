#pragma once

#include <ostream>
#include <string>
#include <vector>
#include "../alpaca/AlpacaApi.hpp"
#include "../quant/MonteCarloRisk.hpp"
#include "../quant/MovingAverageCrossover.hpp"
#include "../quant/PairsTrading.hpp"

/**
 * QuantOptions
 *
 * quant_app's command line: the project to run, the stocks, the dates, and that project's settings
 * (anything not given keeps the default in RiskSettings / PairsSettings / CrossoverSettings).
 *
 *   quant_app risk --symbols AAPL,MSFT --horizon 21
 *   quant_app pairs --pair KO,PEP --entry 2
 */
struct QuantOptions
{
    enum class Project { Risk, Pairs, Scan, Crossover };

    static inline const std::string DEFAULT_START = AlpacaApi::FIRST_HISTORY_DAY;

    Project project = Project::Risk;
    bool showHelp = false;
    std::vector<std::string> symbols;   // the project's default stocks unless given
    std::string start = DEFAULT_START;
    std::string end;                    // empty: up to yesterday
    bool offline = false;               // use the saved CSVs instead of downloading

    quant::RiskSettings risk;
    quant::PairsSettings pairs;
    quant::CrossoverSettings crossover;

    // Throws std::invalid_argument with a readable message for unknown projects, options or bad values
    static QuantOptions parse(int argc, char* argv[]);
    static void printUsage(std::ostream& out);
};
