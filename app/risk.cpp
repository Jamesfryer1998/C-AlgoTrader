#include <iostream>
#include <stdexcept>
#include "../src/research/RiskComparison.hpp"
#include "../src/research/RiskOptions.hpp"
#include "../src/research/RiskTuning.hpp"
#include "../src/util/Config.hpp"
#include "../src/util/Defaults.hpp"
#include "../src/util/DotEnv.hpp"
#include "../src/util/Log.hpp"

/*
 * Which risk engine (position sizing) to trade with, and with which settings? Replays the portfolio in
 * config.json once per engine (RiskComparison), or tunes each engine's settings on training days and checks
 * them on later test days (RiskTuning, --tune), saving the winner with --save.
 */

int main(int argc, char* argv[])
{
    RiskOptions options;
    try
    {
        options = RiskOptions::parse(argc, argv);
    }
    catch (const std::invalid_argument& e)
    {
        std::cerr << e.what() << "\n\n";
        RiskOptions::printUsage(std::cerr);
        return 1;
    }
    if (options.replay.showHelp)
    {
        RiskOptions::printUsage(std::cout);
        return 0;
    }

    try
    {
        Log::verbose = false;   // many replays: no per-bar notes
        Config config;
        std::string root = config.getProjectRoot();
        DotEnv::load(root + "/.env");
        json algoConfig = config.loadConfig();
        std::string dataRoot = root + "/" + algoConfig.value("marketDataBasePath", Defaults::DATA_FOLDER);

        if (options.tune)
            return RiskTuning(algoConfig, dataRoot, root + JSON_CONFIG_NAME, options).run(std::cout);
        return RiskComparison(algoConfig, dataRoot, options).run(std::cout);
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR - " << e.what() << "\n";
        return 1;
    }
}
