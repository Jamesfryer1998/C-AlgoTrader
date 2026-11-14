#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../src/morning/MorningRun.hpp"
#include "../src/util/Config.hpp"
#include "../src/util/Defaults.hpp"
#include "../src/util/DotEnv.hpp"
#include "../src/util/SaveData.hpp"

/*
 * The morning screen: does trading each day's "stocks in play" (unusual volume in the first 5 minutes) make
 * money, compared with the same rules on random stocks? Rules in config/morning.json; see MorningRun.
 */

namespace
{
    const std::string MORNING_CONFIG = "/config/morning.json";
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
        MorningRun::printUsage(std::cerr);
        return 1;
    }
    if (options.showHelp)
    {
        MorningRun::printUsage(std::cout);
        return 0;
    }

    try
    {
        Config config;
        DotEnv::load(config.getProjectRoot() + "/.env");
        json algoConfig = config.loadConfig();
        std::ifstream morningFile(config.getProjectRoot() + MORNING_CONFIG);
        json morningConfig = json::parse(morningFile);
        std::string dataRoot = config.getProjectRoot() + "/" + algoConfig.value("marketDataBasePath", Defaults::DATA_FOLDER);
        SaveData::configure(dataRoot, algoConfig.value("data_limit_gb", SaveData::DEFAULT_LIMIT_GB));
        return MorningRun(algoConfig, morningConfig, dataRoot).run(options);
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR - " << e.what() << std::endl;
        return 1;
    }
}
