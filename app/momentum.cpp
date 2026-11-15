#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../src/morning/MorningRun.hpp"
#include "../src/momentum/MomentumRun.hpp"
#include "../src/util/Config.hpp"
#include "../src/util/Defaults.hpp"
#include "../src/util/DotEnv.hpp"
#include "../src/util/SaveData.hpp"

/*
 * Intraday momentum: do the published rules (the noise-area breakout, the last half hour, overnight) make
 * money on SPY, QQQ and IWM, before and since they were published? Rules in config/momentum.json; see
 * MomentumRun. Same options as morning_app.
 */

namespace
{
    const std::string MOMENTUM_CONFIG = "/config/momentum.json";
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
        MomentumRun::printUsage(std::cerr);
        return 1;
    }
    if (options.showHelp)
    {
        MomentumRun::printUsage(std::cout);
        return 0;
    }

    try
    {
        Config config;
        DotEnv::load(config.getProjectRoot() + "/.env");
        json algoConfig = config.loadConfig();
        std::ifstream momentumFile(config.getProjectRoot() + MOMENTUM_CONFIG);
        json momentumConfig = json::parse(momentumFile);
        std::string dataRoot = config.getProjectRoot() + "/" + algoConfig.value("marketDataBasePath", Defaults::DATA_FOLDER);
        SaveData::configure(dataRoot, algoConfig.value("data_limit_gb", SaveData::DEFAULT_LIMIT_GB));
        return MomentumRun(algoConfig, momentumConfig, dataRoot).run(options);
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR - " << e.what() << std::endl;
        return 1;
    }
}
