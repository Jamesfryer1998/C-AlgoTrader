#include <iostream>
#include <stdexcept>
#include "../src/research/RiskComparison.hpp"
#include "../src/util/Config.hpp"
#include "../src/util/Defaults.hpp"
#include "../src/util/DotEnv.hpp"
#include "../src/util/Log.hpp"

/*
 * Which risk engine (position sizing) to trade with? Replays the same days once per engine, with the
 * portfolio in config.json, and compares return, drawdown, Sharpe and capital used. See RiskComparison.
 */

int main(int argc, char* argv[])
{
    LiveOptions options;
    try
    {
        options = RiskComparison::parse(argc, argv);
    }
    catch (const std::invalid_argument& e)
    {
        std::cerr << e.what() << "\n\n";
        RiskComparison::printUsage(std::cerr);
        return 1;
    }
    if (options.showHelp)
    {
        RiskComparison::printUsage(std::cout);
        return 0;
    }

    try
    {
        Log::verbose = false;   // many replays: no per-bar notes
        Config config;
        DotEnv::load(config.getProjectRoot() + "/.env");
        json algoConfig = config.loadConfig();
        std::string dataRoot = config.getProjectRoot() + "/" + algoConfig.value("marketDataBasePath", Defaults::DATA_FOLDER);

        RiskComparison comparison(algoConfig, dataRoot, options);
        return comparison.run(std::cout);
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR - " << e.what() << "\n";
        return 1;
    }
}
