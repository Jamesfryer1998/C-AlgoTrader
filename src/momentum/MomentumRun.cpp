#include "MomentumRun.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <iostream>
#include <memory>
#include "MomentumDay.hpp"
#include "MomentumSettings.hpp"
#include "MomentumTrader.hpp"
#include "../morning/MorningData.hpp"
#include "../morning/MorningReport.hpp"
#include "../morning/ResearchProgress.hpp"
#include "../morning/StockSelection.hpp"
#include "../util/SaveData.hpp"

namespace fs = std::filesystem;

namespace
{
    const std::string MOMENTUM_FOLDER = "momentum";
    const std::string REPORT_FILE = "report.json";
    const std::string TITLE = "INTRADAY MOMENTUM, AS PUBLISHED";
    const std::string CHOSEN = "rule";
    const std::string COUNT_LABEL = "symbols";

    const std::string STAGE_DAILY = "Daily bars";
    const std::string STAGE_TRADING = "Downloading and trading each day";
    const std::string STAGE_REPORT = "Writing the report";
    const int REPORT_INDENT = 1;

    struct Runner
    {
        std::string symbol;
        std::unique_ptr<MomentumTrader> trader;
        MorningBacktest::Line* line = nullptr;
        MorningBacktest::Line* control = nullptr;
        std::mt19937_64 random;
    };
}

void
MomentumRun::printUsage(std::ostream& out)
{
    out << "Usage: momentum_app [--holdout] [--offline]\n\n"
        << "Backtests the published intraday momentum rules (config/momentum.json) on 1-minute bars of SPY, QQQ\n"
        << "and IWM, against the same trades in random directions. Saves data/momentum/report.json.\n\n"
        << "  --holdout   also trade the holdout: since the rules were published. Do this once, at the end\n"
        << "  --offline   don't download: use only what's in data/momentum/\n";
}

MomentumRun::MomentumRun(const json& algoConfig, const json& momentumConfig, const std::string& dataRoot)
    : algoConfig(algoConfig), momentumConfig(momentumConfig), dataRoot(dataRoot),
      folder((fs::path(dataRoot) / MOMENTUM_FOLDER).string())
{
}

int
MomentumRun::run(const MorningRun::Options& options)
{
    MomentumSettings settings = MomentumSettings::fromConfig(momentumConfig, ResearchProgress::yesterday());
    fs::create_directories(folder);
    ResearchProgress progress(folder);
    progress.save(ResearchProgress::RUNNING, STAGE_DAILY, 0, 1, true);

    std::string holdoutOpened = MorningHoldout::openedAt(folder);
    if (options.holdout)
    {
        std::vector<std::string> names;
        for (const auto& symbol : settings.symbols)
            for (const auto& variant : settings.variants)
                names.push_back(MomentumSettings::lineName(symbol, variant));
        holdoutOpened = MorningHoldout::open(folder, names);
    }

    // 1. The trading days, from the benchmark's daily bars
    MorningData data(algoConfig, folder, options.offline);
    std::vector<std::string> daily = settings.symbols;
    if (std::find(daily.begin(), daily.end(), settings.benchmark) == daily.end())
        daily.push_back(settings.benchmark);
    if (!data.ensureDailyBars(daily, settings.dataStart, settings.end, [&](size_t done, size_t total) {
            progress.stage(STAGE_DAILY, done, total);
        }))
        std::cerr << "WARNING - Some daily bars couldn't be downloaded" << std::endl;
    std::string lastDate = options.holdout ? settings.end : settings.validationEnd;
    std::vector<quant::DailyBar> calendar;
    for (const auto& bar : data.dailyBars(settings.benchmark))
        if (bar.date >= settings.dataStart && bar.date <= lastDate)
            calendar.push_back(bar);
    if (calendar.empty())
    {
        std::cerr << "ERROR - No daily bars for " << settings.benchmark << ", the trading calendar" << std::endl;
        progress.save(ResearchProgress::FAILED, "", 0, 0, true);
        return 1;
    }

    // 2. Every variant on every symbol, a day at a time, against its own trades in random directions
    MorningBacktest::Results results;
    results.title = TITLE;
    results.chosen = CHOSEN;
    results.countLabel = COUNT_LABEL;
    results.symbols = static_cast<int>(settings.symbols.size());
    results.holdoutTraded = options.holdout;
    results.lines.reserve(2 * settings.symbols.size() * settings.variants.size() + 1);
    TradingCosts costs(algoConfig);
    std::vector<Runner> runners;
    for (const auto& symbol : settings.symbols)
        for (const auto& variant : settings.variants)
        {
            std::string name = MomentumSettings::lineName(symbol, variant);
            for (const auto& selection : {CHOSEN, MorningBacktest::RANDOM})
            {
                MorningBacktest::Line line;
                line.variant = name;
                line.selection = selection;
                line.equity = settings.startingCapital;
                results.lines.push_back(line);
            }
            Runner runner;
            runner.symbol = symbol;
            runner.trader = std::make_unique<MomentumTrader>(variant, symbol, costs);
            runner.line = &results.lines[results.lines.size() - 2];
            runner.control = &results.lines.back();
            runner.random.seed(StockSelection::stableHash(name) ^ settings.randomSeed);
            runners.push_back(std::move(runner));
        }

    std::vector<MomentumTrader::Trade> trades;
    for (size_t d = 0; d < calendar.size(); d++)
    {
        progress.stage(STAGE_TRADING, d, calendar.size());
        const std::string& date = calendar[d].date;
        bool record = date >= settings.start;
        auto minutes = data.minuteBars(date, settings.symbols);
        std::map<std::string, MomentumDay> days;
        for (const auto& symbol : settings.symbols)
            days.emplace(symbol, MomentumDay(minutes[symbol]));
        results.daysTraded += record ? 1 : 0;
        for (auto& runner : runners)
        {
            double pnl = runner.trader->day(days[runner.symbol], date, runner.line->equity, record, *runner.line, trades);
            if (!record)
                continue;
            double controlPnl = runner.trader->replay(trades, date, runner.control->equity, runner.random, *runner.control);
            for (auto* line : {runner.line, runner.control})
            {
                double result = line == runner.line ? pnl : controlPnl;
                line->days.push_back(date);
                line->returns.push_back(line->equity > 0 ? result / line->equity : 0);
                line->equity += result;
            }
        }
    }

    // 3. The benchmark bought and held
    MorningBacktest::Line held;
    held.variant = MorningBacktest::BENCHMARK;
    held.selection = settings.benchmark;
    held.equity = settings.startingCapital;
    for (size_t d = 1; d < calendar.size(); d++)
        if (calendar[d].date >= settings.start)
        {
            double change = calendar[d].close / calendar[d - 1].close - 1;
            held.days.push_back(calendar[d].date);
            held.returns.push_back(change);
            held.equity *= 1 + change;
        }
    results.lines.push_back(held);

    // 4. The verdicts
    progress.save(ResearchProgress::RUNNING, STAGE_REPORT, 0, 1, true);
    MorningReport report(settings.forReport(), results);
    report.print(std::cout);
    SaveData::write((fs::path(folder) / REPORT_FILE).string(),
                    report.toJson(holdoutOpened, data.downloads()).dump(REPORT_INDENT));
    std::cout << "Saved " << (fs::path(folder) / REPORT_FILE).string() << " (" << data.downloads()
              << " requests to Alpaca)" << std::endl;
    progress.save(ResearchProgress::FINISHED, STAGE_REPORT, 1, 1, true);
    return 0;
}
