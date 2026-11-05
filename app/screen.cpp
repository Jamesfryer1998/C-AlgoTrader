#include <iostream>
#include <stdexcept>
#include "../src/research/ScreenOptions.hpp"
#include "../src/research/StockScreen.hpp"
#include "../src/util/Config.hpp"
#include "../src/util/DotEnv.hpp"
#include "../src/util/Log.hpp"

/*
 * Strategy screen: which stock / strategy combinations look worth trading?
 *
 * Every stock in src/config/screen.json (the S&P 100 by default) x every strategy (RSI, MACD, MEANREV, SCALP).
 * Each strategy is tuned per stock on a training period, then only that pick is run on a test period it
 * never saw, and compared with buying and holding the stock. Ends with the top picks and the Monte Carlo
 * risk of trading them together. See StockScreen and Screener.
 *
 *   default   1-minute bars (the live app's bars): the last 24 months, the final 6 being the test period.
 *             Settings are in minutes, so the picks can go straight into config.json.
 *   --daily   daily bars since 2016, tested from 2024. Settings are in days.
 */

int main(int argc, char* argv[])
{
    ScreenOptions options;
    try
    {
        options = ScreenOptions::parse(argc, argv);
    }
    catch (const std::invalid_argument& e)
    {
        std::cerr << e.what() << "\n\n";
        ScreenOptions::printUsage(std::cerr);
        return 1;
    }
    if (options.showHelp)
    {
        ScreenOptions::printUsage(std::cout);
        return 0;
    }

    try
    {
        Log::verbose = false;   // thousands of backtests: no per-bar output
        Config config;
        std::string root = config.getProjectRoot();
        DotEnv::load(root + "/.env");
        json baseConfig = config.loadConfig();
        std::string dataRoot = root + "/" + baseConfig.value("marketDataBasePath", "data/");

        StockScreen screen(baseConfig, root, dataRoot, options);
        return screen.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR - " << e.what() << "\n";
        return 1;
    }
}
