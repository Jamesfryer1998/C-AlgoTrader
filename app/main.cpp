#include <csignal>
#include <iostream>
#include "../src/live/LiveOptions.hpp"
#include "../src/live/LiveTrader.hpp"
#include "../src/util/Config.hpp"
#include "../src/util/DotEnv.hpp"

/*
 * Live trading on Alpaca, or a replay of saved bars with a simulated broker (--replay). See LiveTrader.
 * Alpaca keys are read from .env in the project root (copy .env.example to .env and fill it in).
 */

static void onStopSignal(int)
{
    LiveTrader::requestStop();
}

int main(int argc, char* argv[])
{
    LiveOptions options;
    try
    {
        options = LiveOptions::parse(argc, argv);
    }
    catch (const std::invalid_argument& e)
    {
        std::cerr << e.what() << "\n\n";
        LiveOptions::printUsage(std::cerr);
        return 1;
    }
    if (options.showHelp)
    {
        LiveOptions::printUsage(std::cout);
        return 0;
    }

    std::signal(SIGINT, onStopSignal);   // Ctrl+C
    std::signal(SIGTERM, onStopSignal);  // systemctl stop / kill

    Config config;
    DotEnv::load(config.getProjectRoot() + "/.env");
    json algoConfig = config.loadConfig();
    std::string dataRoot = config.getProjectRoot() + "/" + algoConfig.value("marketDataBasePath", "data/");

    LiveTrader trader(algoConfig, options, dataRoot);
    return trader.run();
}
