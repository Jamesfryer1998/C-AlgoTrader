#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "../src/alpaca/AlpacaDailyBars.hpp"
#include "../src/backtest/Screener.hpp"
#include "../src/quant/MonteCarloRisk.hpp"
#include "../src/util/Config.hpp"
#include "../src/util/DotEnv.hpp"
#include "../src/util/Log.hpp"

/*
 * Strategy screen: which stock / strategy combinations look worth trading?
 *
 * Every stock in src/config/screen.json (the S&P 100 by default) x every strategy (RSI, MACD, MEANREV), on
 * daily bars since 2016. Each strategy is tuned per stock on the training years, then only that pick is run
 * on the test years it never saw, and compared with buying and holding the stock.
 *
 * Results: data/screen/screen_results.csv (one row per stock and strategy) and a printed report.
 */

static void printUsage()
{
    std::cout <<
R"(Strategy screen: tune RSI / MACD / MEANREV per stock on the training years, judge on the test years

Usage: screen_app [options]
  --symbols AAPL,MSFT,...   stocks to screen (default: "universe" in src/config/screen.json, the S&P 100)
  --strategies RSI,MEANREV  strategies to try (default: all in screen.json)
  --test-start 2024-01-01   first day of the test years (default: from screen.json)
  --max-combinations 200    try a fixed random sample of each grid instead of all of it (faster)
  --threads 8               backtests at once (default: all CPU cores)
  --offline                 use the daily prices already in data/daily/ instead of downloading
  --resume                  keep stocks already in data/screen/screen_results.csv and screen the rest
  --report                  just print the report from data/screen/screen_results.csv
  --top 5                   how many combinations to take forward and risk-check together (default 5)
  --value 100000            money split across the top picks, for the risk numbers in $ (default 100000)
)";
}

static std::vector<std::string> split(const std::string& text)
{
    std::vector<std::string> parts;
    std::stringstream in(text);
    std::string part;
    while (std::getline(in, part, ','))
        if (!part.empty())
            parts.push_back(part);
    return parts;
}

static std::string fixed(double value, int decimals = 2)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(decimals) << value;
    return out.str();
}

static std::string pct(double fraction, int decimals = 0)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(decimals) << (fraction >= 0 ? "+" : "") << fraction * 100.0 << "%";
    return out.str();
}

static std::string share(int part, int whole)
{
    return std::to_string(part) + (whole > 0 ? " (" + fixed(100.0 * part / whole, 0) + "%)" : "");
}

static void printRows(const std::vector<Screener::Row>& rows)
{
    std::cout << "  " << std::left << std::setw(7) << "Stock" << std::setw(9) << "Strategy" << std::right
              << std::setw(8) << "Train" << std::setw(8) << "Test" << std::setw(9) << "B&H" << std::setw(10) << "Test"
              << std::setw(9) << "B&H" << std::setw(8) << "Test" << "  Settings\n"
              << "  " << std::setw(16) << "" << std::setw(8) << "Sharpe" << std::setw(8) << "Sharpe" << std::setw(9)
              << "Sharpe" << std::setw(10) << "return*" << std::setw(9) << "return" << std::setw(8) << "trades" << "\n";
    for (const auto& r : rows)
        std::cout << "  " << std::left << std::setw(7) << r.symbol << std::setw(9) << r.strategy << std::right
                  << std::setw(8) << fixed(r.train.sharpeRatio) << std::setw(8) << fixed(r.test.sharpeRatio)
                  << std::setw(9) << fixed(r.buyHoldSharpe) << std::setw(10) << pct(r.testReturnOnMaxPosition)
                  << std::setw(9) << pct(r.buyHoldReturn) << std::setw(8) << r.test.closedTrades << "  "
                  << Screener::describe(r.parameters) << "\n";
}

static void printReport(const std::vector<Screener::Row>& rows, const std::vector<std::string>& strategies)
{
    std::set<std::string> stocks;
    std::string testStart, testEnd;
    for (const auto& r : rows)
    {
        stocks.insert(r.symbol);
        if (!r.testStart.empty() && (testStart.empty() || r.testStart < testStart)) testStart = r.testStart;
        if (r.testEnd > testEnd) testEnd = r.testEnd;
    }

    std::cout << "\n=== Strategy screen: " << stocks.size() << " stocks x " << strategies.size() << " strategies ===\n"
              << "Each strategy tuned per stock on the years before " << testStart << ", then judged on "
              << testStart << " to " << testEnd << " (never used for tuning).\n\n";

    // 1. Per strategy
    std::cout << "By strategy\n"
              << "  Strategy  Tradable  Made money   Beat B&H  Median Sharpe    Train->test\n"
              << "                      (test)       (test)    train   test     correlation\n";
    for (const auto& s : Screener::summarise(rows, strategies))
        std::cout << "  " << std::left << std::setw(9) << s.strategy << std::right << std::setw(9)
                  << (std::to_string(s.found) + "/" + std::to_string(s.stocks)) << std::setw(12)
                  << share(s.positiveTest, s.found) << std::setw(11) << share(s.beatBuyHold, s.found)
                  << std::setw(8) << fixed(s.medianTrainSharpe) << std::setw(7) << fixed(s.medianTestSharpe)
                  << std::setw(13) << fixed(s.trainTestCorrelation) << "\n";
    std::cout << "  Tradable: some setting made at least min_closed_trades trades in training. Beat B&H: better Sharpe\n"
              << "  than buying and holding that stock over the test years. Correlation near 0 means a good training\n"
              << "  result said nothing about the test years.\n";

    // 2. Does picking by past results work?
    std::vector<Screener::Row> picks = Screener::topByTraining(rows, 10);
    double allTest = 0, pickTest = 0;
    int allCount = 0, pickBeat = 0;
    for (const auto& r : rows)
        if (r.found) { allTest += r.test.sharpeRatio; allCount++; }
    for (const auto& r : picks)
    {
        pickTest += r.test.sharpeRatio;
        if (r.beatsBuyHold()) pickBeat++;
    }
    if (!picks.empty() && allCount > 0)
    {
        std::cout << "\nThe 10 best combinations on the training years (what you'd pick knowing only the past)\n";
        printRows(picks);
        std::cout << "  * return on a full position (max_position_size shares at the first test price)\n\n"
                  << "  Their average test Sharpe: " << fixed(pickTest / picks.size()) << " vs "
                  << fixed(allTest / allCount) << " for all " << allCount << " tradable combinations; "
                  << pickBeat << " of " << picks.size() << " beat buy and hold.\n"
                  << "  If the top 10 don't beat the average, the best backtests were mostly luck.\n";
    }

    // 3. Held up in both periods
    std::vector<Screener::Row> held = Screener::heldUp(rows, 0.5, 0.5, 3);
    std::cout << "\nHeld up: training Sharpe >= 0.5, test Sharpe >= 0.5 with 3+ trades, and beat buy and hold (" << held.size()
              << " found)\n";
    if (held.size() > 20)
        held.resize(20);
    if (!held.empty())
        printRows(held);
    std::cout << "  These are candidates to paper trade, not proof: with " << allCount
              << " combinations tried, some will pass by luck, and this list was filtered using the test years.\n";
}

static std::string dollars(double value)
{
    long long whole = static_cast<long long>(std::llround(std::fabs(value)));
    std::string digits = std::to_string(whole);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
        digits.insert(static_cast<size_t>(i), ",");
    return (value < 0 ? "-$" : "$") + digits;
}

// The top picks, and the Monte Carlo risk of trading them together
static void printTopPicks(const std::vector<Screener::Row>& rows, const Screener& screener, AlpacaDailyBars& source,
                          const std::string& start, size_t n, double value, const std::string& outDir)
{
    std::vector<Screener::Row> picks = Screener::topPicks(rows, n);
    std::map<std::string, std::vector<quant::DailyBar>> daily;
    for (const auto& r : picks)
        daily[r.symbol] = source.load(r.symbol, start, "", true);   // saved by the screen, no download

    std::cout << "\n=== Top " << n << " to take forward ===\n"
              << "Held up in both periods (Sharpe >= 0.5 in training and test, beat buy and hold, 5+ test trades),\n"
              << "ranked by the weaker of the two Sharpe ratios so both periods count, one strategy per stock.\n";
    if (picks.empty())
    {
        std::cout << "  Nothing passed. That is a real result: none of the tuned strategies did well enough on\n"
                  << "  years they weren't tuned on to be worth trading over buying and holding.\n";
        return;
    }
    if (picks.size() < n)
        std::cout << "  Only " << picks.size() << " passed.\n";

    std::cout << "\n  #  Stock  Strategy   Train   Test    B&H    Test     Test  Settings\n"
              << "                        Sharpe  Sharpe  Sharpe return*  trades\n";
    for (size_t i = 0; i < picks.size(); i++)
    {
        const auto& r = picks[i];
        std::cout << "  " << i + 1 << "  " << std::left << std::setw(7) << r.symbol << std::setw(9) << r.strategy << std::right
                  << std::setw(7) << fixed(r.train.sharpeRatio) << std::setw(8) << fixed(r.test.sharpeRatio)
                  << std::setw(7) << fixed(r.buyHoldSharpe) << std::setw(8) << pct(r.testReturnOnMaxPosition)
                  << std::setw(8) << r.test.closedTrades << "  " << Screener::describe(r.parameters) << "\n";
    }
    std::cout << "  * return on a full position (max_position_size shares at the first test price)\n";

    std::ofstream file(outDir + "/top_picks.csv");
    file << "rank,symbol,strategy,parameters,train_sharpe,test_sharpe,buy_hold_sharpe,test_return_on_max_position,test_closed_trades\n";
    for (size_t i = 0; i < picks.size(); i++)
    {
        const auto& r = picks[i];
        std::string params = r.parameters.dump();
        std::string quotedParams = "\"";
        for (char c : params) quotedParams += (c == '"') ? std::string("\"\"") : std::string(1, c);
        quotedParams += "\"";
        file << i + 1 << "," << r.symbol << "," << r.strategy << "," << quotedParams << "," << r.train.sharpeRatio << ","
             << r.test.sharpeRatio << "," << r.buyHoldSharpe << "," << r.testReturnOnMaxPosition << ","
             << r.test.closedTrades << "\n";
    }

    // Risk of running them together: each pick's daily P&L stream, simulated forward a month at a time
    if (picks.size() < 2)
        return;
    quant::PriceTable values = screener.pickValues(picks, daily);
    if (values.days() < 60 || values.symbols.size() < 2)
    {
        std::cout << "\n  (Not enough overlapping history to measure their combined risk.)\n";
        return;
    }

    quant::RiskSettings settings;
    settings.portfolioValue = value;
    quant::RiskResult risk = quant::MonteCarloRisk::run(values, settings);
    size_t k = risk.symbols.size();

    std::cout << "\nRisk of trading these " << k << " together (Monte Carlo, " << settings.simulations
              << " simulated months, " << dollars(value) << " split equally)\n"
              << "From each pick's daily P&L with its chosen setting, " << values.dates.front() << " to " << values.dates.back()
              << ", as a return on a full position.\n\n"
              << "  " << std::left << std::setw(16) << "Pick" << std::right << std::setw(11) << "Return/yr" << std::setw(9) << "Vol/yr" << "\n";
    for (size_t i = 0; i < k; i++)
        std::cout << "  " << std::left << std::setw(16) << risk.symbols[i] << std::right << std::setw(11)
                  << pct(risk.annualReturn[i], 1) << std::setw(9) << pct(risk.annualVolatility[i], 1).substr(1) << "\n";

    std::cout << "\n  Correlation of daily P&L\n  " << std::setw(16) << "";
    for (size_t j = 0; j < k; j++)
        std::cout << std::setw(7) << ("#" + std::to_string(j + 1));
    std::cout << "\n";
    for (size_t i = 0; i < k; i++)
    {
        std::cout << "  " << std::left << std::setw(16) << ("#" + std::to_string(i + 1) + " " + risk.symbols[i]) << std::right;
        for (size_t j = 0; j < k; j++)
            std::cout << std::setw(7) << fixed(risk.correlation[i][j]);
        std::cout << "\n";
    }

    std::cout << "\n  Combined volatility " << pct(risk.portfolioAnnualVolatility, 1).substr(1) << "/yr (if uncorrelated: "
              << pct(risk.portfolioAnnualVolatilityUncorrelated, 1).substr(1) << ")\n"
              << "  Over a month:   average " << pct(risk.meanReturn, 1) << ", chance of a loss " << fixed(risk.probabilityOfLoss * 100, 0) << "%\n"
              << "  VaR 95%         " << std::setw(6) << pct(risk.at95.var, 1).substr(1) << "  " << std::setw(9) << dollars(risk.at95.var * value)
              << "   1 month in 20 loses at least this\n"
              << "  Exp. shortfall  " << std::setw(6) << pct(risk.at95.es, 1).substr(1) << "  " << std::setw(9) << dollars(risk.at95.es * value)
              << "   average loss in those worst months\n"
              << "  VaR 99%         " << std::setw(6) << pct(risk.at99.var, 1).substr(1) << "  " << std::setw(9) << dollars(risk.at99.var * value) << "\n"
              << "  ES 99%          " << std::setw(6) << pct(risk.at99.es, 1).substr(1) << "  " << std::setw(9) << dollars(risk.at99.es * value) << "\n"
              << "  Real months that lost more than VaR 95%: " << fixed(risk.historicalBreachRate95 * 100, 1)
              << "% (model: 5%). Worst real month: " << pct(risk.worstHistorical, 1) << " from " << risk.worstHistoricalStart << ".\n"
              << "  The training years are in this history and the settings were fitted to them, so expect returns to be\n"
              << "  lower than shown; the volatility and correlations are the more trustworthy part.\n";
}

int main(int argc, char* argv[])
{
    std::vector<std::string> symbols, strategies;
    std::string testStart;
    int threads = std::max(1u, std::thread::hardware_concurrency());
    int maxCombinations = -1;
    size_t topN = 5;
    double portfolioValue = 100000;
    bool offline = false, resume = false, reportOnly = false;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
            return argv[++i];
        };
        try
        {
            if (arg == "--help") { printUsage(); return 0; }
            else if (arg == "--symbols") symbols = split(value());
            else if (arg == "--strategies") strategies = split(value());
            else if (arg == "--test-start") testStart = value();
            else if (arg == "--max-combinations") maxCombinations = std::stoi(value());
            else if (arg == "--threads") threads = std::max(1, std::stoi(value()));
            else if (arg == "--offline") offline = true;
            else if (arg == "--resume") resume = true;
            else if (arg == "--report") reportOnly = true;
            else if (arg == "--top") topN = static_cast<size_t>(std::max(1, std::stoi(value())));
            else if (arg == "--value") portfolioValue = std::stod(value());
            else { std::cerr << "Unknown argument: " << arg << "\n"; printUsage(); return 1; }
        }
        catch (const std::exception& e)
        {
            std::cerr << e.what() << "\n";
            return 1;
        }
    }

    try
    {
        Log::verbose = false;   // thousands of backtests: no per-bar output
        Config config;
        std::string root = config.getProjectRoot();
        DotEnv::load(root + "/.env");
        json baseConfig = config.loadConfig();

        std::ifstream file(root + "/src/config/screen.json");
        if (!file) { std::cerr << "Missing src/config/screen.json\n"; return 1; }
        json settings = json::parse(file, nullptr, true, true);
        if (!testStart.empty()) settings["test_start"] = testStart;
        if (maxCombinations >= 0) settings["max_combinations"] = maxCombinations;
        if (symbols.empty()) symbols = settings.at("universe").get<std::vector<std::string>>();
        if (strategies.empty())
            for (const auto& [name, grid] : settings.at("strategies").items())
                strategies.push_back(name);

        std::string dataRoot = root + "/" + baseConfig.value("marketDataBasePath", "data/");
        std::string outDir = dataRoot + "screen";
        std::filesystem::create_directories(outDir);
        std::string resultsPath = outDir + "/screen_results.csv";

        Screener screener(baseConfig, settings);
        AlpacaDailyBars source(baseConfig, dataRoot + "daily");
        std::string start = settings.value("start", "2016-01-01");

        if (reportOnly)
        {
            std::vector<Screener::Row> rows = Screener::readCsv(resultsPath);
            if (rows.empty()) { std::cerr << "No results in " << resultsPath << " yet\n"; return 1; }
            printReport(rows, strategies);
            printTopPicks(rows, screener, source, start, topN, portfolioValue, outDir);
            return 0;
        }

        std::vector<Screener::Row> rows = resume ? Screener::readCsv(resultsPath) : std::vector<Screener::Row>();
        std::set<std::string> done;
        for (const auto& r : rows)
            done.insert(r.symbol);

        size_t combinations = 0;
        for (const auto& name : strategies)
            combinations += screener.getOptimiser().parameterGrid(name).size();
        std::cout << "Screening " << symbols.size() << " stocks x " << strategies.size() << " strategies ("
                  << combinations << " settings per stock), test years from " << settings.value("test_start", "2024-01-01")
                  << ", " << threads << " threads\n";
        if (resume && !done.empty())
            std::cout << "Resuming: " << done.size() << " stocks already done\n";

        std::vector<std::string> skipped;
        auto started = std::chrono::steady_clock::now();

        for (size_t s = 0; s < symbols.size(); s++)
        {
            const std::string& symbol = symbols[s];
            if (done.count(symbol))
                continue;

            std::vector<quant::DailyBar> daily = source.load(symbol, start, "", offline);
            if (daily.empty())
            {
                std::cerr << "Skipping " << symbol << ": no daily data\n";
                skipped.push_back(symbol);
                continue;
            }

            auto stockStarted = std::chrono::steady_clock::now();
            std::vector<Screener::Row> stockRows = screener.screenStock(symbol, daily, strategies, threads);
            double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - stockStarted).count();

            std::cout << "[" << std::setw(3) << s + 1 << "/" << symbols.size() << "] " << std::left << std::setw(6)
                      << symbol << std::right;
            for (const auto& r : stockRows)
                std::cout << "  " << r.strategy << " "
                          << (r.found ? fixed(r.train.sharpeRatio) + "->" + fixed(r.test.sharpeRatio) : std::string("-"));
            if (!stockRows.empty())
                std::cout << "  | B&H " << fixed(stockRows.front().buyHoldSharpe);
            std::cout << "  (" << fixed(seconds, 0) << "s)" << std::endl;

            rows.insert(rows.end(), stockRows.begin(), stockRows.end());
            Screener::writeCsv(resultsPath, rows);   // saved as we go, so --resume can pick up from here
        }

        double minutes = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() / 60.0;
        std::cout << "Sharpe shown as training -> test. Finished in " << fixed(minutes, 1) << " minutes.\n";
        if (!skipped.empty())
        {
            std::cout << "Skipped (no data): ";
            for (const auto& s : skipped) std::cout << s << " ";
            std::cout << "\n";
        }

        printReport(rows, strategies);
        printTopPicks(rows, screener, source, start, topN, portfolioValue, outDir);
        std::cout << "\nAll results: " << resultsPath << " (top picks: " << outDir << "/top_picks.csv)\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR - " << e.what() << "\n";
        return 1;
    }
}
