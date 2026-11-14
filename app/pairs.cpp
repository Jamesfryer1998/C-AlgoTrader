#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../src/morning/MorningRun.hpp"
#include "../src/pairs/PairsRun.hpp"
#include "../src/util/Config.hpp"
#include "../src/util/Defaults.hpp"
#include "../src/util/DotEnv.hpp"
#include "../src/util/SaveData.hpp"

/*
 * Pairs and lead-lag: do stocks that move together pay to trade against each other, compared with random
 * pairs? Rules in config/pairs.json; see PairsRun. Same options as morning_app.
 */

namespace
{
    const std::string PAIRS_CONFIG = "/config/pairs.json";
}

int main(int argc, char* argv[])
{
    MorningRun::Options options;
    try
    {
        options = MorningRun::parse(std::vector<std::string>(argv + 1, argv + argc));
    }
    catch (const std::invalid_argument& e)
    {
        std::cerr << e.what() << "\n\n";
        PairsRun::printUsage(std::cerr);
        return 1;
    }
    if (options.showHelp)
    {
        PairsRun::printUsage(std::cout);
        return 0;
    }

    try
    {
        Config config;
        DotEnv::load(config.getProjectRoot() + "/.env");
        json algoConfig = config.loadConfig();
        std::ifstream pairsFile(config.getProjectRoot() + PAIRS_CONFIG);
        json pairsConfig = json::parse(pairsFile);
        std::string dataRoot = config.getProjectRoot() + "/" + algoConfig.value("marketDataBasePath", Defaults::DATA_FOLDER);
        SaveData::configure(dataRoot, algoConfig.value("data_limit_gb", SaveData::DEFAULT_LIMIT_GB));
        return PairsRun(algoConfig, pairsConfig, dataRoot).run(options);
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR - " << e.what() << std::endl;
        return 1;
    }
}
