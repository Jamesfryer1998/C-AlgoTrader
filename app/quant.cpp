#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "../src/alpaca/AlpacaDailyBars.hpp"
#include "../src/quant/MonteCarloRisk.hpp"
#include "../src/quant/MovingAverageCrossover.hpp"
#include "../src/quant/PairsTrading.hpp"
#include "../src/util/Config.hpp"
#include "../src/util/DotEnv.hpp"

/*
 * Three quant finance learning projects on daily data from Alpaca (cached in data/daily/):
 *
 *   quant_app risk     Monte Carlo portfolio risk: VaR and expected shortfall, and why correlation matters
 *   quant_app pairs    Pairs trading: cointegration test, hedge ratio, z-score backtest
 *   quant_app scan     Find cointegrated pairs in a list of stocks
 *   quant_app macross  50/200-day moving average crossover vs buy and hold, without look-ahead, with costs
 *
 * Results are printed and saved as CSVs in data/quant/.
 */

using namespace quant;

static void printUsage()
{
    std::cout <<
R"(Quant learning projects (daily data from Alpaca, keys in .env)

Usage: quant_app <project> [options]

  risk      Monte Carlo portfolio risk
              --symbols AAPL,MSFT,NVDA,JPM,XOM   stocks in the portfolio
              --weights 0.3,0.2,0.2,0.2,0.1      (default: equal)
              --horizon 21                       trading days ahead (21 = about a month)
              --sims 10000                       number of simulated futures
              --value 100000                     portfolio value in $
              --seed 42

  pairs     Pairs trading backtest
              --pair KO,PEP          Y,X: regresses log Y on log X
              --lookback 252         days used to fit the hedge ratio and z-score
              --entry 2 --exit 0.5 --stop 4      z-score levels (--stop 0 = no stop)
              --cost-bps 5           cost per trade (0.01% units) on the gross position

  scan      Test every pair in a list for cointegration
              --symbols KO,PEP,XOM,CVX,V,MA,HD,LOW,UPS,FDX,GS,MS

  macross   Moving average crossover backtest
              --symbol SPY --fast 50 --slow 200 --cost-bps 5

Common options:
  --start 2016-01-01   first day of data (Alpaca's history starts in 2016)
  --end YYYY-MM-DD     last day (default: yesterday)
  --offline            use the CSVs already in data/daily/ instead of downloading
  --delay 1            days between a signal and the returns it earns (pairs/macross; 0 = look-ahead)
)";
}

// ---------- small helpers ----------

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

static std::string pct(double fraction, int decimals = 1, bool sign = false)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(decimals) << (sign && fraction >= 0 ? "+" : "") << fraction * 100.0 << "%";
    return out.str();
}

static std::string dollars(double value)
{
    std::ostringstream out;
    long long whole = static_cast<long long>(std::llround(std::fabs(value)));
    std::string digits = std::to_string(whole);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
        digits.insert(static_cast<size_t>(i), ",");
    out << (value < 0 ? "-$" : "$") << digits;
    return out.str();
}

static std::string fixed(double value, int decimals = 2)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(decimals) << value;
    return out.str();
}

static std::string days(double halfLife)
{
    return std::isfinite(halfLife) ? fixed(halfLife, 0) + "d" : "never";
}

struct Options
{
    std::map<std::string, std::string> values;
    bool has(const std::string& key) const { return values.count(key) > 0; }
    std::string get(const std::string& key, const std::string& fallback) const
    {
        auto it = values.find(key);
        return it != values.end() ? it->second : fallback;
    }
    double number(const std::string& key, double fallback) const
    {
        return has(key) ? std::stod(values.at(key)) : fallback;
    }
};

static void printPerformanceHeader()
{
    std::cout << "  " << std::left << std::setw(30) << "" << std::right << std::setw(9) << "Total" << std::setw(9) << "CAGR"
              << std::setw(9) << "Vol" << std::setw(8) << "Sharpe" << std::setw(9) << "MaxDD" << std::endl;
}

static void printPerformance(const std::string& name, const Performance& p)
{
    std::cout << "  " << std::left << std::setw(30) << name << std::right << std::setw(9) << pct(p.totalReturn, 0, true)
              << std::setw(9) << pct(p.cagr, 1, true) << std::setw(9) << pct(p.annualVolatility) << std::setw(8)
              << fixed(p.sharpe) << std::setw(9) << pct(-p.maxDrawdown) << std::endl;
}

// ---------- 1. Monte Carlo risk ----------

static int runRisk(const Options& options, const PriceTable& prices, const std::string& outDir)
{
    RiskSettings settings;
    for (const auto& w : split(options.get("weights", "")))
        settings.weights.push_back(std::stod(w));
    settings.horizonDays = static_cast<int>(options.number("horizon", 21));
    settings.simulations = static_cast<int>(options.number("sims", 10000));
    settings.portfolioValue = options.number("value", 100000);
    settings.seed = static_cast<unsigned>(options.number("seed", 42));

    RiskResult r = MonteCarloRisk::run(prices, settings);
    size_t k = r.symbols.size();

    std::cout << "\n=== Monte Carlo portfolio risk ===\n"
              << prices.dates.front() << " to " << prices.dates.back() << " (" << prices.days() << " trading days)\n\n";

    std::cout << "  " << std::left << std::setw(8) << "Stock" << std::right << std::setw(8) << "Weight"
              << std::setw(12) << "Return/yr" << std::setw(10) << "Vol/yr" << std::endl;
    for (size_t i = 0; i < k; i++)
        std::cout << "  " << std::left << std::setw(8) << r.symbols[i] << std::right << std::setw(8) << pct(r.weights[i], 0)
                  << std::setw(12) << pct(r.annualReturn[i], 1, true) << std::setw(10) << pct(r.annualVolatility[i]) << std::endl;

    std::cout << "\n  Correlation of daily returns\n  " << std::setw(8) << "";
    for (const auto& s : r.symbols)
        std::cout << std::setw(7) << s;
    std::cout << std::endl;
    for (size_t i = 0; i < k; i++)
    {
        std::cout << "  " << std::left << std::setw(8) << r.symbols[i] << std::right;
        for (size_t j = 0; j < k; j++)
            std::cout << std::setw(7) << fixed(r.correlation[i][j]);
        std::cout << std::endl;
    }

    std::cout << "\n  Portfolio volatility " << pct(r.portfolioAnnualVolatility) << "/yr  (if the stocks were uncorrelated: "
              << pct(r.portfolioAnnualVolatilityUncorrelated) << ")\n";

    double v = settings.portfolioValue;
    std::cout << "\n" << settings.simulations << " simulated " << settings.horizonDays << "-day futures for "
              << dollars(v) << ":\n"
              << "  Average return     " << pct(r.meanReturn, 1, true) << "\n"
              << "  Chance of a loss   " << pct(r.probabilityOfLoss, 0) << "\n"
              << "  VaR 95%            " << std::setw(6) << pct(r.at95.var) << "  " << std::setw(9) << dollars(r.at95.var * v)
              << "   1 in 20 periods loses at least this\n"
              << "  Expected shortfall " << std::setw(6) << pct(r.at95.es) << "  " << std::setw(9) << dollars(r.at95.es * v)
              << "   average loss in those worst 5%\n"
              << "  VaR 99%            " << std::setw(6) << pct(r.at99.var) << "  " << std::setw(9) << dollars(r.at99.var * v)
              << "   1 in 100\n"
              << "  ES 99%             " << std::setw(6) << pct(r.at99.es) << "  " << std::setw(9) << dollars(r.at99.es * v)
              << "\n";

    std::cout << "\nCorrelation drives the risk: the same stocks with zero correlation would have VaR 95% "
              << pct(r.uncorrelated95.var) << " and ES " << pct(r.uncorrelated95.es) << " (vs " << pct(r.at95.var)
              << " and " << pct(r.at95.es) << ").\n";
    std::cout << "Reality check over " << r.historicalWindows << " real " << settings.horizonDays
              << "-day windows: losses beyond VaR 95% happened " << pct(r.historicalBreachRate95)
              << " of the time (model: 5%), beyond VaR 99% " << pct(r.historicalBreachRate99)
              << " (model: 1%).\nWorst real window: " << pct(r.worstHistorical, 1, true) << " starting "
              << r.worstHistoricalStart << ".\n";

    std::string path = outDir + "/risk_simulations.csv";
    std::ofstream file(path);
    file << "simulation,return,pnl\n";
    for (size_t i = 0; i < r.simulatedReturns.size(); i++)
        file << i + 1 << "," << r.simulatedReturns[i] << "," << r.simulatedReturns[i] * v << "\n";
    std::cout << "\nSaved " << path << " (simulated returns, worst first)\n";
    return 0;
}

// ---------- 2. Pairs trading ----------

static int runPairs(const Options& options, const PriceTable& prices, const std::string& outDir)
{
    PairsSettings settings;
    settings.lookbackDays = static_cast<int>(options.number("lookback", 252));
    settings.entryZ = options.number("entry", 2.0);
    settings.exitZ = options.number("exit", 0.5);
    settings.stopZ = options.number("stop", 4.0);
    settings.costBps = options.number("cost-bps", 5.0);
    settings.signalDelayDays = static_cast<int>(options.number("delay", 1));

    const std::string& yName = prices.symbols[0];
    const std::string& xName = prices.symbols[1];
    PairsResult r = PairsTrading::backtest(yName, xName, prices.dates, prices[0], prices[1], settings);

    std::cout << "\n=== Pairs trading: " << yName << " vs " << xName << " ===\n"
              << prices.dates.front() << " to " << prices.dates.back() << " (" << prices.days() << " trading days)\n\n";

    std::cout << "Whole period: log " << yName << " = " << fixed(r.fullSample.alpha, 3) << " + " << fixed(r.fullSample.beta, 3)
              << " x log " << xName << "\n"
              << "  Cointegrated? " << r.fullSample.verdict() << "   ADF " << fixed(r.fullSample.adf)
              << " (needs < " << EG_CRITICAL_5 << " for 95%)   half-life " << days(r.fullSample.halfLifeDays)
              << "   daily return correlation " << fixed(r.returnCorrelation) << "\n";

    std::cout << "\nYear by year (relationships break):\n  Year   Hedge     ADF  Half-life  Cointegrated?\n";
    for (const auto& [year, test] : r.byYear)
        std::cout << "  " << year << std::setw(8) << fixed(test.beta) << std::setw(8) << fixed(test.adf)
                  << std::setw(11) << days(test.halfLifeDays) << "  " << test.verdict() << "\n";

    int reverted = 0, stopped = 0, wins = 0, holding = 0;
    for (const auto& t : r.trades)
    {
        if (t.exitReason == "reverted") reverted++;
        if (t.exitReason == "stop") stopped++;
        if (t.returnPct > 0) wins++;
        holding += t.days;
    }

    std::cout << "\nBacktest: " << settings.lookbackDays << "-day rolling fit, enter |z| > " << fixed(settings.entryZ, 1)
              << ", exit |z| < " << fixed(settings.exitZ, 1)
              << (settings.stopZ > 0 ? ", stop |z| > " + fixed(settings.stopZ, 1) : std::string(", no stop"))
              << ", cost " << fixed(settings.costBps, 1) << " bps, signal delay " << settings.signalDelayDays << " day\n";
    std::cout << "  Trades " << r.trades.size() << " (" << reverted << " reverted, " << stopped << " stopped)";
    if (!r.trades.empty())
        std::cout << ", won " << pct(static_cast<double>(wins) / r.trades.size(), 0) << ", average "
                  << holding / static_cast<int>(r.trades.size()) << " days held";
    std::cout << ", in the market " << pct(r.timeInMarket, 0) << " of the time\n\n";
    printPerformanceHeader();
    printPerformance("Pair strategy (after costs)", r.performance);

    if (!r.trades.empty())
    {
        std::cout << "\n  Recent trades\n  Entry       Exit        Side                  z in  z out  Days   Return  Why\n";
        size_t from = r.trades.size() > 10 ? r.trades.size() - 10 : 0;
        for (size_t i = from; i < r.trades.size(); i++)
        {
            const auto& t = r.trades[i];
            std::string side = t.side > 0 ? "buy " + yName + "/sell " + xName : "sell " + yName + "/buy " + xName;
            std::cout << "  " << std::left << std::setw(12) << t.entryDate << std::setw(12) << t.exitDate << std::setw(22) << side
                      << std::right << std::setw(5) << fixed(t.entryZ, 1) << std::setw(7) << fixed(t.exitZ, 1)
                      << std::setw(6) << t.days << std::setw(9) << pct(t.returnPct, 1, true) << "  " << t.exitReason << "\n";
        }
    }

    std::string base = outDir + "/pairs_" + yName + "_" + xName;
    std::ofstream daily(base + "_daily.csv");
    daily << "date,z,hedge_ratio,position,return\n";
    for (size_t i = 0; i < r.dates.size(); i++)
        daily << r.dates[i] << "," << r.z[i] << "," << r.hedgeRatio[i] << "," << r.position[i] << "," << r.dailyReturns[i] << "\n";
    std::ofstream trades(base + "_trades.csv");
    trades << "entry_date,exit_date,side,hedge_ratio,entry_z,exit_z,days,return,exit_reason\n";
    for (const auto& t : r.trades)
        trades << t.entryDate << "," << t.exitDate << "," << t.side << "," << t.hedgeRatio << "," << t.entryZ << ","
               << t.exitZ << "," << t.days << "," << t.returnPct << "," << t.exitReason << "\n";
    std::cout << "\nSaved " << base << "_daily.csv and " << base << "_trades.csv\n";
    return 0;
}

static int runScan(const PriceTable& prices, const std::string& outDir)
{
    std::vector<PairScan> rows = PairsTrading::scan(prices);

    std::cout << "\n=== Pair scan: Engle-Granger cointegration on log prices ===\n"
              << prices.dates.front() << " to " << prices.dates.back() << " (" << prices.days() << " trading days)\n\n"
              << "  Y     X        ADF  Cointegrated?  Hedge  Half-life  Return corr\n";
    size_t shown = std::min<size_t>(rows.size(), 15);
    for (size_t i = 0; i < shown; i++)
    {
        const PairScan& row = rows[i];
        std::cout << "  " << std::left << std::setw(6) << row.y << std::setw(6) << row.x << std::right << std::setw(7)
                  << fixed(row.test.adf) << "  " << std::left << std::setw(13) << row.test.verdict() << std::right
                  << std::setw(7) << fixed(row.test.beta) << std::setw(11) << days(row.test.halfLifeDays)
                  << std::setw(13) << fixed(row.returnCorrelation) << "\n";
    }
    if (shown < rows.size())
        std::cout << "  ... " << rows.size() - shown << " more in the CSV\n";

    std::cout << "\nTesting " << rows.size() << " pairs at 95% finds about " << fixed(rows.size() * 0.05, 1)
              << " 'cointegrated' by chance alone. Check a candidate year by year with: quant_app pairs --pair Y,X\n";

    std::string path = outDir + "/pair_scan.csv";
    std::ofstream file(path);
    file << "y,x,adf,verdict,hedge_ratio,half_life_days,return_correlation\n";
    for (const auto& row : rows)
        file << row.y << "," << row.x << "," << row.test.adf << "," << row.test.verdict() << "," << row.test.beta << ","
             << row.test.halfLifeDays << "," << row.returnCorrelation << "\n";
    std::cout << "Saved " << path << "\n";
    return 0;
}

// ---------- 3. Moving average crossover ----------

static int runCrossover(const Options& options, const PriceTable& prices, const std::string& outDir)
{
    CrossoverSettings settings;
    settings.fastDays = static_cast<int>(options.number("fast", 50));
    settings.slowDays = static_cast<int>(options.number("slow", 200));
    settings.costBps = options.number("cost-bps", 5.0);
    settings.signalDelayDays = static_cast<int>(options.number("delay", 1));

    const std::string& symbol = prices.symbols[0];
    CrossoverResult r = MovingAverageCrossover::backtest(prices.dates, prices[0], settings);

    std::cout << "\n=== Moving average crossover: " << symbol << ", " << settings.fastDays << "/" << settings.slowDays
              << "-day ===\n" << r.dates.front() << " to " << r.dates.back() << " (" << r.dates.size()
              << " trading days, from the first day both averages exist)\n"
              << "Invested while the " << settings.fastDays << "-day average is above the " << settings.slowDays
              << "-day, otherwise cash. Cost " << fixed(settings.costBps, 1) << " bps per buy or sell.\n\n";

    printPerformanceHeader();
    printPerformance("Crossover (" + std::to_string(settings.signalDelayDays) + "-day delay, costs)", r.strategy);
    printPerformance("  same, without costs", r.strategyNoCosts);
    printPerformance("  same, with look-ahead", r.lookAhead);
    printPerformance("Buy and hold", r.buyHold);

    std::cout << "\n  " << r.trades << " buys, invested " << pct(r.timeInMarket, 0) << " of the time, costs "
              << pct(r.costsPaid, 2) << " of capital in total\n";

    double cagrGap = r.strategy.cagr - r.buyHold.cagr;
    std::cout << "\nVerdict: the crossover " << (cagrGap >= 0 ? "beat" : "lagged") << " buy and hold by "
              << pct(std::fabs(cagrGap), 1) << "/yr, Sharpe " << fixed(r.strategy.sharpe) << " vs "
              << fixed(r.buyHold.sharpe) << ", worst drawdown " << pct(r.strategy.maxDrawdown, 0) << " vs "
              << pct(r.buyHold.maxDrawdown, 0) << ".\n"
              << "Look-ahead (deciding with the same close it trades on) changes CAGR by "
              << pct(r.lookAhead.cagr - r.strategy.cagr, 1, true) << "/yr: any gain there is profit nobody could have had.\n";

    std::string path = outDir + "/macross_" + symbol + ".csv";
    std::ofstream file(path);
    file << "date,close,fast_ma,slow_ma,position,strategy_return,buy_hold_return\n";
    for (size_t i = 0; i < r.dates.size(); i++)
        file << r.dates[i] << "," << r.closes[i] << "," << r.fastAverage[i] << "," << r.slowAverage[i] << ","
             << r.position[i] << "," << r.strategyReturns[i] << "," << r.buyHoldReturns[i] << "\n";
    std::cout << "Saved " << path << "\n";
    return 0;
}

// ---------- main ----------

int main(int argc, char* argv[])
{
    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "help")
    {
        printUsage();
        return argc < 2 ? 1 : 0;
    }
    std::string project = argv[1];

    Options options;
    for (int i = 2; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg.rfind("--", 0) != 0)
        {
            std::cerr << "Unexpected argument: " << arg << "\n";
            return 1;
        }
        std::string key = arg.substr(2);
        if (key == "offline" || key == "help")
            options.values[key] = "1";
        else if (i + 1 < argc)
            options.values[key] = argv[++i];
        else
        {
            std::cerr << "Missing value for " << arg << "\n";
            return 1;
        }
    }
    if (options.has("help"))
    {
        printUsage();
        return 0;
    }

    std::vector<std::string> symbols;
    if (project == "risk")
        symbols = split(options.get("symbols", "AAPL,MSFT,NVDA,JPM,XOM"));
    else if (project == "pairs")
        symbols = split(options.get("pair", "KO,PEP"));
    else if (project == "scan")
        symbols = split(options.get("symbols", "KO,PEP,XOM,CVX,V,MA,HD,LOW,UPS,FDX,GS,MS"));
    else if (project == "macross")
        symbols = {options.get("symbol", "SPY")};
    else
    {
        std::cerr << "Unknown project: " << project << "\n\n";
        printUsage();
        return 1;
    }
    if ((project == "pairs" && symbols.size() != 2) || (project == "scan" && symbols.size() < 2) || symbols.empty())
    {
        std::cerr << "Wrong number of symbols for " << project << " (pairs needs exactly 2, e.g. --pair KO,PEP)\n";
        return 1;
    }

    try
    {
        Config config;
        DotEnv::load(config.getProjectRoot() + "/.env");
        json algoConfig = config.loadConfig();
        std::string dataRoot = config.getProjectRoot() + "/" + algoConfig.value("marketDataBasePath", "data/");
        std::string outDir = dataRoot + "quant";
        std::filesystem::create_directories(outDir);

        std::string start = options.get("start", "2016-01-01");
        std::string end = options.get("end", "");

        AlpacaDailyBars source(algoConfig, dataRoot + "daily");
        std::map<std::string, std::vector<DailyBar>> bars;
        if (!source.loadAll(symbols, start, end, options.has("offline"), bars))
            return 1;
        PriceTable prices = alignCloses(symbols, bars, start, end);
        if (prices.days() < 2)
        {
            std::cerr << "ERROR - The symbols have no trading days in common in that range\n";
            return 1;
        }

        if (project == "risk")
            return runRisk(options, prices, outDir);
        if (project == "pairs")
            return runPairs(options, prices, outDir);
        if (project == "scan")
            return runScan(prices, outDir);
        return runCrossover(options, prices, outDir);
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR - " << e.what() << "\n";
        return 1;
    }
}
