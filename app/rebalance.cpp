#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../src/morning/MorningRun.hpp"
#include "../src/rebalance/RebalanceRun.hpp"
#include "../src/util/Config.hpp"
#include "../src/util/Defaults.hpp"
#include "../src/util/DotEnv.hpp"
#include "../src/util/SaveData.hpp"

/*
 * Month-end rebalancing: does trading ahead of 60/40 funds' month-end rebalancing pay, before and since the
 * paper that found it? Rules in config/rebalance.json; see RebalanceRun. Same options as morning_app.
 */

namespace
{
    const std::string REBALANCE_CONFIG = "/config/rebalance.json";
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
        RebalanceRun::printUsage(std::cerr);
        return 1;
    }
    if (options.showHelp)
    {
        RebalanceRun::printUsage(std::cout);
        return 0;
    }

    try
    {
        Config config;
        DotEnv::load(config.getProjectRoot() + "/.env");
        json algoConfig = config.loadConfig();
        std::ifstream rebalanceFile(config.getProjectRoot() + REBALANCE_CONFIG);
        json rebalanceConfig = json::parse(rebalanceFile);
        std::string dataRoot = config.getProjectRoot() + "/" + algoConfig.value("marketDataBasePath", Defaults::DATA_FOLDER);
        SaveData::configure(dataRoot, algoConfig.value("data_limit_gb", SaveData::DEFAULT_LIMIT_GB));
        return RebalanceRun(algoConfig, rebalanceConfig, dataRoot).run(options);
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR - " << e.what() << std::endl;
        return 1;
    }
}
