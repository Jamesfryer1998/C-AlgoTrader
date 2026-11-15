#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../src/morning/MorningRun.hpp"
#include "../src/intraday/IntradayRun.hpp"
#include "../src/util/Config.hpp"
#include "../src/util/Defaults.hpp"
#include "../src/util/DotEnv.hpp"
#include "../src/util/SaveData.hpp"

/*
 * Intraday statistical arbitrage: do stocks that stray from their sector fund in the day come back (or keep
 * going), hedged, many times a day, better than random stocks? Rules in config/intraday.json; see IntradayRun. Same options as morning_app.
 */

namespace
{
    const std::string INTRADAY_CONFIG = "/config/intraday.json";
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
        IntradayRun::printUsage(std::cerr);
        return 1;
    }
    if (options.showHelp)
    {
        IntradayRun::printUsage(std::cout);
        return 0;
    }

    try
    {
        Config config;
        DotEnv::load(config.getProjectRoot() + "/.env");
        json algoConfig = config.loadConfig();
        std::ifstream intradayFile(config.getProjectRoot() + INTRADAY_CONFIG);
        json intradayConfig = json::parse(intradayFile);
        std::string dataRoot = config.getProjectRoot() + "/" + algoConfig.value("marketDataBasePath", Defaults::DATA_FOLDER);
        SaveData::configure(dataRoot, algoConfig.value("data_limit_gb", SaveData::DEFAULT_LIMIT_GB));
        return IntradayRun(algoConfig, intradayConfig, dataRoot).run(options);
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR - " << e.what() << std::endl;
        return 1;
    }
}
