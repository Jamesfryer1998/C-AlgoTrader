#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include "../src/alpaca/AlpacaHistory.hpp"
#include "../src/backtest/MinuteBars.hpp"
#include "../src/backtest/Optimiser.hpp"
#include "../src/data_access/CSVParser.hpp"
#include "../src/data_access/DataStitcher.hpp"
#include "../src/util/Config.hpp"
#include "../src/util/Format.hpp"
#include "../src/util/DotEnv.hpp"
#include "../src/util/Log.hpp"
#include "../src/util/MarketTime.hpp"
#include "../src/alpaca/AlpacaApi.hpp"
#include "../src/util/Defaults.hpp"
#include "../src/util/SaveData.hpp"

/*
 * optimise_app - find the best settings for each strategy
 *
 *   ./build/app/optimise_app                         all strategies, all downloaded data since 2016
 *   ./build/app/optimise_app --strategy RSI          one strategy
 *   ./build/app/optimise_app --download              fetch any missing data from Alpaca first
 *
 * What to try is set in config/optimisation.json. Results go to data/optimisation/.
 */

namespace fs = std::filesystem;

static void printUsage()
{
    std::cout << "optimise_app - backtest every combination of strategy settings and rank them\n\n"
              << "Options:\n"
              << "  --strategy <RSI|MACD|MEANREV|SCALP|all>  Which strategy to optimise (default: all)\n"
              << "  --start-date <YYYY-MM-DD>          First day of data (default: 2016-01-01)\n"
              << "  --end-date <YYYY-MM-DD>            Last day of data (default: yesterday)\n"
              << "  --test-months <n>                  Months at the end kept back for testing (default: from optimisation.json)\n"
              << "  --max-combinations <n>             Try a random sample of n settings per strategy (default: all)\n"
              << "  --threads <n>                      Backtests run at once (default: all CPU cores)\n"
              << "  --download                         Download missing data from Alpaca first (uses .env keys)\n"
              << "  --help\n";
}

static std::string describe(const json& parameters)
{
    std::string text;
    for (const auto& [key, value] : parameters.items())
    {
        std::string v = value.is_string() ? value.get<std::string>() : value.dump();
        text += (text.empty() ? "" : " ") + key + "=" + v;
    }
    return text;
}

static void writeResultsCsv(const std::string& path, const Optimiser& optimiser,
                            const std::vector<Optimiser::Result>& results, const Optimiser::Result& current)
{
    std::ofstream file(path);
    std::vector<std::string> parameters;
    if (!results.empty())
    {
        json searched = optimiser.searchedParameters(results[0].settings);   // keep it alive while we loop
        for (const auto& [key, value] : searched.items())
            parameters.push_back(key);
    }

    file << "rank";
    for (const auto& p : parameters) file << "," << p;
    file << ",eligible,train_sharpe,train_return_pct,train_max_drawdown_pct,train_closed_trades,train_win_rate_pct,"
            "train_profit_factor,tested,test_sharpe,test_return_pct,test_max_drawdown_pct,test_closed_trades,test_win_rate_pct\n";

    auto row = [&](const std::string& rank, const Optimiser::Result& r) {
        file << rank;
        for (const auto& p : parameters)
        {
            std::string text;
            if (r.settings.contains(p))
                text = r.settings[p].is_string() ? r.settings[p].get<std::string>() : r.settings[p].dump();
            file << "," << text;
        }
        file << "," << r.eligible << "," << r.train.sharpeRatio << "," << r.train.roiPercent << ","
             << r.train.maxDrawdownPercent << "," << r.train.closedTrades << "," << r.train.winRatePercent << ","
             << r.train.profitFactor << "," << r.tested;
        if (r.tested)
            file << "," << r.test.sharpeRatio << "," << r.test.roiPercent << "," << r.test.maxDrawdownPercent << ","
                 << r.test.closedTrades << "," << r.test.winRatePercent;
        else
            file << ",,,,,";
        file << "\n";
    };

    row("current_config", current);
    for (size_t i = 0; i < results.size(); i++)
        row(std::to_string(i + 1), results[i]);
}

int main(int argc, char* argv[])
{
    std::string strategyArg = "all";
    std::string startDate = AlpacaApi::FIRST_HISTORY_DAY;
    std::string endDate;
    int testMonths = -1;
    long maxCombinations = -1;
    int threads = std::max(1u, std::thread::hardware_concurrency());
    bool download = false;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        auto value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--help") { printUsage(); return 0; }
        else if (arg == "--strategy") strategyArg = value();
        else if (arg == "--start-date") startDate = value();
        else if (arg == "--end-date") endDate = value();
        else if (arg == "--test-months") testMonths = std::stoi(value());
        else if (arg == "--max-combinations") maxCombinations = std::stol(value());
        else if (arg == "--threads") threads = std::max(1, std::stoi(value()));
        else if (arg == "--download") download = true;
        else { std::cerr << "Unknown argument: " << arg << "\n\n"; printUsage(); return 1; }
    }
    if (endDate.empty())
        endDate = MarketTime::datePart(MarketTime::formatUtc(std::time(nullptr) - 24 * 3600));

    Log::verbose = false;   // thousands of backtests: no per-bar or per-file output

    // ---- Settings ----
    Config config;
    std::string root = config.getProjectRoot();
    json baseConfig = config.loadConfig();
    json settings;
    {
        std::ifstream file(config.formConfigPath(ConfigFiles::OPTIMISATION));
        if (!file) { std::cerr << "Missing " << ConfigFiles::OPTIMISATION << std::endl; return 1; }
        settings = json::parse(file, nullptr, true, true);
    }
    if (testMonths >= 0) settings["test_months"] = testMonths;
    if (maxCombinations >= 0) settings["max_combinations"] = maxCombinations;

    std::vector<std::string> strategies;
    for (const auto& [name, ranges] : settings["strategies"].items())
        if (strategyArg == "all" || strategyArg == name)
            strategies.push_back(name);
    if (strategies.empty()) { std::cerr << "Unknown strategy " << strategyArg << std::endl; return 1; }

    std::string ticker = baseConfig.at("ticker");
    std::string dataDir = root + "/" + baseConfig.value("marketDataBasePath", Defaults::DATA_FOLDER);
    SaveData::configure(dataDir, baseConfig.value("data_limit_gb", SaveData::DEFAULT_LIMIT_GB));

    // ---- Data ----
    if (download)
    {
        DotEnv::load(root + "/.env");
        if (AlpacaHistory(baseConfig).downloadToCsv(startDate, endDate, dataDir) < 0)
        {
            std::cerr << "Download failed - check your keys in .env" << std::endl;
            return 1;
        }
    }

    std::cout << "Loading " << ticker << " bars " << startDate << " to " << endDate << " from " << dataDir << "..." << std::endl;
    std::vector<MarketCondition> bars = MinuteBars::load(dataDir, ticker, startDate, endDate, threads);
    if (bars.empty())
    {
        std::cerr << "No data found. Download it first:\n  ./build/app/optimise_app --download\n";
        return 1;
    }

    std::vector<MarketCondition> train, test;
    Optimiser::splitByMonths(bars, settings.value("test_months", 12), train, test);
    std::cout << "Loaded " << bars.size() << " bars (" << bars.front().DateTime.substr(0, 10) << " to "
              << bars.back().DateTime.substr(0, 10) << ")\n"
              << "  Training: " << train.size() << " bars"
              << (train.empty() ? "" : " (" + train.front().DateTime.substr(0, 10) + " to " + train.back().DateTime.substr(0, 10) + ")") << "\n"
              << "  Testing:  " << test.size() << " bars"
              << (test.empty() ? "" : " (" + test.front().DateTime.substr(0, 10) + " to " + test.back().DateTime.substr(0, 10) + ")") << "\n"
              << "Costs: $" << settings.value("commission_per_trade", 0.0) << " commission, "
              << settings.value("slippage_percent", 0.01) << "% slippage per fill | objective: "
              << settings.value("objective", "sharpe") << " | " << threads << " threads\n" << std::endl;

    Optimiser optimiser(baseConfig, settings);

    fs::create_directories(dataDir + "optimisation");
    std::string stamp = MarketTime::formatUtc(std::time(nullptr)).substr(0, 19);
    std::replace(stamp.begin(), stamp.end(), ':', '-');
    json best = json::object();

    for (const auto& name : strategies)
    {
        std::vector<json> grid = optimiser.parameterGrid(name);
        std::cout << "==================== " << name << " ====================\n"
                  << "Trying " << grid.size() << " settings..." << std::endl;

        auto started = std::chrono::steady_clock::now();
        size_t lastShown = 0;
        std::vector<Optimiser::Result> results = optimiser.search(grid, train, test, threads,
            [&](size_t done, size_t total) {
                size_t percent = 100 * done / total;
                if (percent >= lastShown + 10 || done == total)
                {
                    lastShown = percent;
                    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                    std::cout << "  " << done << "/" << total << " (" << percent << "%) " << Format::number(seconds, 0) << "s" << std::endl;
                }
            });
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

        // Current settings in config/strategy.json, for comparison
        Optimiser::Result current;
        current.settings = optimiser.currentSettings(name);
        try
        {
            current.train = optimiser.evaluate(current.settings, train);
            current.eligible = current.train.closedTrades >= settings.value("min_closed_trades", 30);
            current.score = optimiser.objective(current.train);
            if (!test.empty()) { current.test = optimiser.evaluate(current.settings, test); current.tested = true; }
        }
        catch (const std::exception& e)
        {
            std::cout << "  (current settings for " << name << " can't run: " << e.what() << ")" << std::endl;
        }

        std::cout << "Done in " << Format::number(seconds, 0) << "s\n\n"
                  << std::left << std::setw(5) << "Rank" << std::setw(16) << "Train Sharpe" << std::setw(15) << "Train return"
                  << std::setw(14) << "Train trades" << std::setw(15) << "Test Sharpe" << std::setw(14) << "Test return"
                  << "Settings" << std::endl;
        auto line = [&](const std::string& rank, const Optimiser::Result& r) {
            std::cout << std::left << std::setw(5) << rank << std::setw(16) << Format::number(r.train.sharpeRatio)
                      << std::setw(15) << (Format::number(r.train.roiPercent, 3) + "%") << std::setw(14) << r.train.closedTrades
                      << std::setw(15) << (r.tested ? Format::number(r.test.sharpeRatio) : "-")
                      << std::setw(14) << (r.tested ? Format::number(r.test.roiPercent, 3) + "%" : "-")
                      << describe(optimiser.searchedParameters(r.settings)) << std::endl;
        };
        for (size_t i = 0; i < results.size() && i < 10; i++)
            line(std::to_string(i + 1), results[i]);
        line("now", current);

        std::string csvPath = dataDir + "optimisation/" + name + "_" + stamp + ".csv";
        writeResultsCsv(csvPath, optimiser, results, current);

        if (!results.empty() && results[0].eligible)
        {
            const Optimiser::Result& top = results[0];
            best[name] = {
                {"settings", top.settings},
                {"train", PerformanceTracker::toJson(top.train)},
                {"test", top.tested ? PerformanceTracker::toJson(top.test) : json()},
            };
            std::cout << "\nBest on training data: " << describe(optimiser.searchedParameters(top.settings)) << "\n";
            if (top.tested && top.test.sharpeRatio < 0)
                std::cout << "WARNING: its Sharpe ratio on the test data was negative (" << Format::number(top.test.sharpeRatio)
                          << "), so it probably fits the past rather than finding a real edge." << std::endl;
        }
        else
        {
            std::cout << "\nNo setting made at least " << settings.value("min_closed_trades", 30)
                      << " closed trades on the training data, so none is recommended." << std::endl;
        }
        std::cout << "All results: " << csvPath << "\n" << std::endl;
    }

    std::string bestPath = dataDir + "optimisation/best_" + stamp + ".json";
    std::ofstream(bestPath) << best.dump(2) << "\n";
    std::cout << "Best settings per strategy (copy into config/strategy.json if you want them): " << bestPath << std::endl;
    return 0;
}
