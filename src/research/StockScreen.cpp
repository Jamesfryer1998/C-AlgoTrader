#include "StockScreen.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include "ScreenReport.hpp"
#include "TopPicksReport.hpp"
#include "../util/Format.hpp"

using Format::number;

namespace
{
    const std::string SCREEN_SETTINGS = "/src/config/screen.json";   // under the project root
    const std::string RESULTS_FOLDER = "screen/";                    // under data/, then minute/ or daily/
    const std::string RESULTS_FILE = "/screen_results.csv";

    double secondsSince(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

    // "[  3/100] AAPL  "
    std::string counter(size_t index, size_t total, const std::string& symbol)
    {
        std::ostringstream out;
        out << "[" << std::setw(3) << index + 1 << "/" << total << "] " << std::left << std::setw(6) << symbol;
        return out.str();
    }
}

StockScreen::StockScreen(const json& baseConfig, const std::string& projectRoot, const std::string& dataRoot,
                         const ScreenOptions& options)
    : options(options),
      screen(ScreenSettings::load(projectRoot + SCREEN_SETTINGS, options)),
      resultsFolder(dataRoot + RESULTS_FOLDER + screen.mode),
      resultsPath(resultsFolder + RESULTS_FILE),
      screener(baseConfig, screen.settings),
      bars(baseConfig, dataRoot, screen, options.threads)
{
    std::filesystem::create_directories(resultsFolder);
}

int
StockScreen::run()
{
    if (options.downloadOnly)
    {
        downloadAll();
        return 0;
    }

    std::vector<Screener::Row> rows = options.reportOnly ? Screener::readCsv(resultsPath) : screenAll();
    if (rows.empty())
    {
        std::cerr << "No results in " << resultsPath << " yet\n";
        return 1;
    }
    printResults(rows);
    return 0;
}

void
StockScreen::downloadAll()
{
    for (size_t i = 0; i < screen.symbols.size(); i++)
    {
        auto started = std::chrono::steady_clock::now();
        bool downloaded = bars.download(screen.symbols[i]);
        std::cout << counter(i, screen.symbols.size(), screen.symbols[i]) << (downloaded ? " downloaded" : " FAILED")
                  << " (" << number(secondsSince(started), 0) << "s)" << std::endl;
    }
}

std::vector<Screener::Row>
StockScreen::screenAll()
{
    std::vector<Screener::Row> rows = options.resume ? Screener::readCsv(resultsPath) : std::vector<Screener::Row>();
    std::set<std::string> done;
    for (const auto& row : rows)
        done.insert(row.symbol);

    size_t settingsPerStock = 0;
    for (const auto& name : screen.strategies)
        settingsPerStock += screener.getOptimiser().parameterGrid(name).size();
    std::cout << "Screening " << screen.symbols.size() << " stocks x " << screen.strategies.size() << " strategies on "
              << (screen.daily ? "daily" : "1-minute") << " bars (" << settingsPerStock << " settings per stock)\n"
              << "Training " << screen.start << " to " << screen.testStart << ", test " << screen.testStart << " to "
              << screen.end << ", " << options.threads << " threads\n";
    if (!done.empty())
        std::cout << "Resuming: " << done.size() << " stocks already done\n";

    size_t toScreen = 0, screened = 0;
    for (const auto& symbol : screen.symbols)
        if (!done.count(symbol))
            toScreen++;

    std::vector<std::string> skipped;
    auto started = std::chrono::steady_clock::now();
    for (size_t i = 0; i < screen.symbols.size(); i++)
    {
        const std::string& symbol = screen.symbols[i];
        if (done.count(symbol))
            continue;

        if (!options.offline && !bars.download(symbol))
            std::cerr << "WARNING - download of " << symbol << " failed; using whatever is saved" << std::endl;
        std::vector<MarketCondition> stockBars = bars.load(symbol);
        if (stockBars.empty())
        {
            std::cerr << "Skipping " << symbol << ": no data\n";
            skipped.push_back(symbol);
            continue;
        }

        auto stockStarted = std::chrono::steady_clock::now();
        std::vector<Screener::Row> stockRows = screener.screenStock(symbol, stockBars, screen.strategies, options.threads);
        screened++;
        double minutesLeft = secondsSince(started) / screened * (toScreen - screened) / 60.0;
        printStockLine(i, symbol, stockRows, secondsSince(stockStarted), minutesLeft);

        rows.insert(rows.end(), stockRows.begin(), stockRows.end());
        Screener::writeCsv(resultsPath, rows);   // saved as we go, so --resume can pick up from here
    }

    std::cout << "Sharpe shown as training -> test. Finished in " << number(secondsSince(started) / 60.0, 1) << " minutes.\n";
    if (!skipped.empty())
    {
        std::cout << "Skipped (no data):";
        for (const auto& symbol : skipped)
            std::cout << " " << symbol;
        std::cout << "\n";
    }
    return rows;
}

// "[  3/100] AAPL    RSI 0.52->0.10  MACD -  | B&H 0.80  (12s, about 40 min left)"
void
StockScreen::printStockLine(size_t index, const std::string& symbol, const std::vector<Screener::Row>& stockRows,
                            double seconds, double minutesLeft) const
{
    std::cout << counter(index, screen.symbols.size(), symbol);
    for (const auto& row : stockRows)
        std::cout << "  " << row.strategy << " "
                  << (row.found ? number(row.train.sharpeRatio) + "->" + number(row.test.sharpeRatio) : std::string("-"));
    if (!stockRows.empty())
        std::cout << "  | B&H " << number(stockRows.front().buyHoldSharpe);
    std::cout << "  (" << number(seconds, 0) << "s, about " << number(minutesLeft, 0) << " min left)" << std::endl;
}

void
StockScreen::printResults(const std::vector<Screener::Row>& rows)
{
    ScreenReport(rows, screen.strategies).print(std::cout);

    std::map<std::string, std::vector<MarketCondition>> pickBars;
    for (const auto& pick : Screener::topPicks(rows, options.topPicks))
        pickBars[pick.symbol] = bars.load(pick.symbol);   // saved by the screen, no download
    TopPicksReport topPicks(rows, screener, options.topPicks, options.portfolioValue, pickBars);
    topPicks.print(std::cout);
    std::vector<std::string> saved = topPicks.save(resultsFolder);

    std::cout << "\nAll results: " << resultsPath << "\nTop picks: " << saved[0] << " and " << saved[1]
              << " (config.json strategy blocks)\n";
}
