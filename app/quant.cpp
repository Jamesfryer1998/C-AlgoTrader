#include <iostream>
#include <stdexcept>
#include "../src/research/QuantOptions.hpp"
#include "../src/research/QuantProjects.hpp"
#include "../src/util/Config.hpp"
#include "../src/util/DotEnv.hpp"

/*
 * Quant finance learning projects on daily data from Alpaca (cached in data/daily/):
 *
 *   quant_app risk     Monte Carlo portfolio risk: VaR and expected shortfall, and why correlation matters
 *   quant_app pairs    Pairs trading: cointegration test, hedge ratio, z-score backtest
 *   quant_app scan     Find cointegrated pairs in a list of stocks
 *   quant_app macross  50/200-day moving average crossover vs buy and hold, without look-ahead, with costs
 *
 * Results are printed and saved as CSVs in data/quant/. See QuantProjects.
 */

int main(int argc, char* argv[])
{
    QuantOptions options;
    try
    {
        options = QuantOptions::parse(argc, argv);
    }
    catch (const std::invalid_argument& e)
    {
        std::cerr << e.what() << "\n\n";
        QuantOptions::printUsage(std::cerr);
        return 1;
    }
    if (options.showHelp)
    {
        QuantOptions::printUsage(std::cout);
        return argc < 2 ? 1 : 0;
    }

    try
    {
        Config config;
        DotEnv::load(config.getProjectRoot() + "/.env");
        json algoConfig = config.loadConfig();
        std::string dataRoot = config.getProjectRoot() + "/" + algoConfig.value("marketDataBasePath", "data/");

        QuantProjects projects(algoConfig, dataRoot);
        return projects.run(options);
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR - " << e.what() << "\n";
        return 1;
    }
}
