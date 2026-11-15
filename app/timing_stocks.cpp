#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../src/morning/MorningRun.hpp"
#include "../src/timing/TimingStocksRun.hpp"
#include "../src/util/Config.hpp"
#include "../src/util/Defaults.hpp"
#include "../src/util/DotEnv.hpp"
#include "../src/util/SaveData.hpp"

/*
 * Market timing on many stocks: of thousands of rules for when to hold a stock, does any beat holding it, on
 * hundreds of stocks? Rules in config/timing_stocks.json; see TimingStocksRun. Same options as morning_app.
 */

namespace
{
    const std::string TIMING_STOCKS_CONFIG = "/config/timing_stocks.json";
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
        TimingStocksRun::printUsage(std::cerr);
        return 1;
    }
    if (options.showHelp)
    {
        TimingStocksRun::printUsage(std::cout);
        return 0;
    }

    try
    {
        Config config;
        DotEnv::load(config.getProjectRoot() + "/.env");
        json algoConfig = config.loadConfig();
        std::ifstream timingFile(config.getProjectRoot() + TIMING_STOCKS_CONFIG);
        json timingConfig = json::parse(timingFile);
        std::string dataRoot = config.getProjectRoot() + "/" + algoConfig.value("marketDataBasePath", Defaults::DATA_FOLDER);
        SaveData::configure(dataRoot, algoConfig.value("data_limit_gb", SaveData::DEFAULT_LIMIT_GB));
        return TimingStocksRun(algoConfig, timingConfig, dataRoot).run(options);
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR - " << e.what() << std::endl;
        return 1;
    }
}
