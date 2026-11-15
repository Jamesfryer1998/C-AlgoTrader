#include "RebalanceRun.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include "RebalanceBacktest.hpp"
#include "RebalanceSettings.hpp"
#include "../morning/MorningData.hpp"
#include "../morning/MorningReport.hpp"
#include "../morning/ResearchProgress.hpp"
#include "../morning/StockSelection.hpp"
#include "../quant/Stats.hpp"
#include "../util/SaveData.hpp"

namespace fs = std::filesystem;

namespace
{
    const std::string REBALANCE_FOLDER = "rebalance";
    const std::string REPORT_FILE = "report.json";
    const std::string TITLE = "MONTH-END REBALANCING";
    const std::string CHOSEN = "signal";
    const std::string COUNT_LABEL = "funds";

    const std::string STAGE_DATA = "Daily bars";
    const std::string STAGE_TRADING = "Trading each variant";
    const std::string STAGE_REPORT = "Writing the report";
    const int REPORT_INDENT = 1;
    const double PERCENT = 100.0;

    // The Sharpe of `traded` over the days in `period`
    double sharpeIn(const RebalanceBacktest::Traded& traded, const MorningSettings& periods, const std::string& period)
    {
        std::vector<double> returns;
        for (size_t i = 0; i < traded.days.size(); i++)
            if (periods.periodOf(traded.days[i]) == period)
                returns.push_back(traded.returns[i]);
        return returns.empty() ? 0 : quant::performance(returns).sharpe;
    }
}

void
RebalanceRun::printUsage(std::ostream& out)
{
    out << "Usage: rebalance_app [--holdout] [--offline]\n\n"
        << "Backtests front-running the month-end rebalancing of 60/40 funds (config/rebalance.json) on daily bars\n"
        << "of SPY and Treasury funds, against coin-flipped directions. Saves data/rebalance/report.json.\n\n"
        << "  --holdout   also trade the holdout: since the paper came out. Do this once, at the end\n"
        << "  --offline   don't download: use only what's in data/rebalance/\n";
}

RebalanceRun::RebalanceRun(const json& algoConfig, const json& rebalanceConfig, const std::string& dataRoot)
    : algoConfig(algoConfig), rebalanceConfig(rebalanceConfig), dataRoot(dataRoot),
      folder((fs::path(dataRoot) / REBALANCE_FOLDER).string())
{
}

int
RebalanceRun::run(const MorningRun::Options& options)
{
    RebalanceSettings settings = RebalanceSettings::fromConfig(rebalanceConfig, ResearchProgress::yesterday());
    fs::create_directories(folder);
    ResearchProgress progress(folder);
    progress.save(ResearchProgress::RUNNING, STAGE_DATA, 0, 1, true);
    std::string holdoutOpened = MorningHoldout::openedAt(folder);
    if (options.holdout)
    {
        std::vector<std::string> names;
        for (const auto& variant : settings.variants)
            names.push_back(variant.name);
        holdoutOpened = MorningHoldout::open(folder, names);
    }

    // 1. Daily bars, dividends and coupons in, the bonds on the stock fund's days
    json dataConfig = algoConfig;
    dataConfig["alpaca"]["history_adjustment"] = settings.adjustment;
    MorningData data(dataConfig, folder, options.offline);
    std::vector<std::string> symbols = {settings.stocks};
    symbols.insert(symbols.end(), settings.bonds.begin(), settings.bonds.end());
    if (!data.ensureDailyBars(symbols, settings.dataStart, settings.end, [&](size_t done, size_t total) {
            progress.stage(STAGE_DATA, done, total);
        }))
        std::cerr << "WARNING - Some daily bars couldn't be downloaded" << std::endl;
    std::vector<std::string> days;
    std::vector<double> stocks;
    for (const auto& bar : data.dailyBars(settings.stocks))
        if (bar.date >= settings.dataStart && bar.date <= settings.end)
        {
            days.push_back(bar.date);
            stocks.push_back(bar.close);
        }
    if (days.size() < 2)
    {
        std::cerr << "ERROR - No daily bars for " << settings.stocks << std::endl;
        progress.save(ResearchProgress::FAILED, "", 0, 0, true);
        return 1;
    }
    std::map<std::string, std::vector<double>> bonds;
    for (const auto& bond : settings.bonds)
    {
        std::map<std::string, double> closes;
        for (const auto& bar : data.dailyBars(bond))
            closes[bar.date] = bar.close;
        for (const auto& day : days)
            bonds[bond].push_back(closes.count(day) ? closes[day] : 0);
    }

    size_t first = static_cast<size_t>(std::lower_bound(days.begin(), days.end(), settings.start) - days.begin());
    std::string lastDate = options.holdout ? settings.end : settings.validationEnd;
    size_t last = static_cast<size_t>(std::upper_bound(days.begin(), days.end(), lastDate) - days.begin());
    last = last == 0 ? 0 : last - 1;
    RebalanceBacktest backtest(settings, days, stocks, bonds);
    MorningSettings periods = settings.forReport();
    std::vector<std::string> periodNames = {MorningSettings::DEVELOPMENT, MorningSettings::VALIDATION};
    if (options.holdout)
        periodNames.push_back(MorningSettings::HOLDOUT);

    // 2. Each variant, and as many coin-flipped versions of it
    MorningBacktest::Results results;
    results.title = TITLE;
    results.chosen = CHOSEN;
    results.countLabel = COUNT_LABEL;
    results.symbols = static_cast<int>(symbols.size());
    results.daysTraded = first <= last ? static_cast<int>(last - first + 1) : 0;
    results.holdoutTraded = options.holdout;
    std::ostringstream percentiles;
    percentiles << std::fixed << std::setprecision(0) << "\nBEAT COIN-FLIPPED DIRECTIONS (" << settings.randomDraws
                << " draws, by Sharpe)\n";
    for (size_t v = 0; v < settings.variants.size(); v++)
    {
        const auto& variant = settings.variants[v];
        progress.stage(STAGE_TRADING, v, settings.variants.size());
        std::vector<double> held = backtest.positions(variant);
        RebalanceBacktest::Traded traded = backtest.trade(variant, held, first, last);
        std::mt19937_64 random(StockSelection::stableHash(variant.name) ^ settings.randomSeed);
        RebalanceBacktest::Traded firstFlip;
        std::map<std::string, int> beaten;
        for (int draw = 0; draw < settings.randomDraws; draw++)
        {
            RebalanceBacktest::Traded flipped = backtest.trade(variant, backtest.coinFlipped(held, random), first, last);
            for (const auto& period : periodNames)
                beaten[period] += sharpeIn(traded, periods, period) > sharpeIn(flipped, periods, period) ? 1 : 0;
            if (draw == 0)
                firstFlip = flipped;
        }
        std::string symbol = variant.legs == RebalanceSettings::Legs::Spread ? settings.stocks + "-" + variant.bond : settings.stocks;
        results.lines.push_back(backtest.line(traded, variant.name, CHOSEN, symbol));
        results.lines.push_back(backtest.line(firstFlip, variant.name, MorningBacktest::RANDOM, symbol));
        percentiles << "  " << std::left << std::setw(40) << variant.name << std::right;
        for (const auto& period : periodNames)
            percentiles << "  " << period << " " << std::setw(3)
                        << (settings.randomDraws > 0 ? beaten[period] * PERCENT / settings.randomDraws : 0) << "%";
        percentiles << "\n";
    }
    MorningBacktest::Line held;
    held.variant = MorningBacktest::BENCHMARK;
    held.selection = settings.stocks;
    held.equity = settings.startingCapital;
    for (size_t day = std::max<size_t>(first, 1); day <= last && day < days.size(); day++)
    {
        double change = stocks[day] / stocks[day - 1] - 1;
        held.days.push_back(days[day]);
        held.returns.push_back(change);
        held.equity *= 1 + change;
    }
    results.lines.push_back(held);

    // 3. The verdicts
    progress.save(ResearchProgress::RUNNING, STAGE_REPORT, 0, 1, true);
    MorningReport report(periods, results);
    report.print(std::cout);
    SaveData::write((fs::path(folder) / REPORT_FILE).string(),
                    report.toJson(holdoutOpened, data.downloads()).dump(REPORT_INDENT));
    std::cout << percentiles.str();
    std::cout << "Saved " << (fs::path(folder) / REPORT_FILE).string() << " (" << data.downloads()
              << " requests to Alpaca)" << std::endl;
    progress.save(ResearchProgress::FINISHED, STAGE_REPORT, 1, 1, true);
    return 0;
}
