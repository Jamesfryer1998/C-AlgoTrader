#include "GridRun.hpp"

#include <filesystem>
#include <iostream>
#include "GridBacktest.hpp"
#include "GridSettings.hpp"
#include "../morning/MorningReport.hpp"
#include "../morning/ResearchProgress.hpp"
#include "../pairs/DailyPrices.hpp"
#include "../util/SaveData.hpp"

namespace fs = std::filesystem;

namespace
{
    const std::string GRID_FOLDER = "grid";
    const std::string REPORT_FILE = "report.json";
    const std::string TITLE = "BUYING DROPS, SELLING POPS";
    const std::string CHOSEN = "rule";
    const std::string HELD = "held";
    const std::string COUNT_LABEL = "stocks";

    const std::string STAGE_TRADING = "Trading each variant";
    const std::string STAGE_REPORT = "Writing the report";
    const int REPORT_INDENT = 1;
}

void
GridRun::printUsage(std::ostream& out)
{
    out << "Usage: grid_app [--holdout] [--offline]\n\n"
        << "Backtests buying more after a big down day and selling some after a big up day (config/grid.json), on\n"
        << "the most traded stocks and index funds, next to holding the same and coin-flipped directions. Saves\n"
        << "data/grid/report.json.\n\n"
        << "  --holdout   also trade the holdout period. Do this once, when the rules are final (it's recorded)\n"
        << "  --offline   don't download: use only what's in data/pairs/\n";
}

GridRun::GridRun(const json& algoConfig, const json& gridConfig, const std::string& dataRoot)
    : algoConfig(algoConfig), gridConfig(gridConfig), dataRoot(dataRoot),
      folder((fs::path(dataRoot) / GRID_FOLDER).string())
{
}

int
GridRun::run(const MorningRun::Options& options)
{
    GridSettings settings = GridSettings::fromConfig(gridConfig, ResearchProgress::yesterday());
    fs::create_directories(folder);
    ResearchProgress progress(folder);
    progress.save(ResearchProgress::RUNNING, "", 0, 0, true);
    std::string holdoutOpened = MorningHoldout::openedAt(folder);
    if (options.holdout)
    {
        std::vector<std::string> names;
        for (const auto& variant : settings.variants)
            names.push_back(variant.name);
        holdoutOpened = MorningHoldout::open(folder, names);
    }

    // 1. Daily bars (shared with pairs_app), and the stocks and funds traded
    DailyPrices::Request request{settings.dataStart, settings.end, settings.prefilterDollarVolume,
                                 settings.loadDollarVolume, options.offline, settings.funds};
    PriceMatrix prices;
    size_t downloads = 0;
    if (!DailyPrices::load(algoConfig, dataRoot, request, progress, prices, downloads))
    {
        progress.save(ResearchProgress::FAILED, "", 0, 0, true);
        return 1;
    }
    size_t first = prices.indexOnOrAfter(settings.start);
    size_t last = DailyPrices::indexOnOrBefore(prices, options.holdout ? settings.end : settings.validationEnd);
    GridBacktest backtest(settings, prices);
    std::vector<size_t> stocks = backtest.mostTraded(first, settings.stocks);
    std::vector<size_t> funds = backtest.fundRows();
    std::cout << stocks.size() << " stocks and " << funds.size() << " funds" << std::endl;

    // 2. Each variant, next to holding the same and to coin-flipped directions
    MorningBacktest::Results results;
    results.title = TITLE;
    results.chosen = CHOSEN;
    results.countLabel = COUNT_LABEL;
    results.symbols = static_cast<int>(stocks.size() + funds.size());
    results.daysTraded = first <= last ? static_cast<int>(last - first + 1) : 0;
    results.holdoutTraded = options.holdout;
    for (size_t v = 0; v < settings.variants.size(); v++)
    {
        const auto& variant = settings.variants[v];
        progress.stage(STAGE_TRADING, v, settings.variants.size());
        const auto& rows = variant.universe == GridSettings::Universe::Funds ? funds : stocks;
        results.lines.push_back(backtest.run(variant, rows, first, last, GridBacktest::Mode::Rule, CHOSEN));
        results.lines.push_back(backtest.run(variant, rows, first, last, GridBacktest::Mode::Random, MorningBacktest::RANDOM));
        results.lines.push_back(backtest.run(variant, rows, first, last, GridBacktest::Mode::Held, HELD));
    }
    results.lines.push_back(DailyPrices::buyAndHold(prices, first, last, settings.startingCapital));

    // 3. The verdicts
    progress.save(ResearchProgress::RUNNING, STAGE_REPORT, 0, 1, true);
    MorningReport report(settings.forReport(), results);
    report.print(std::cout);
    SaveData::write((fs::path(folder) / REPORT_FILE).string(), report.toJson(holdoutOpened, downloads).dump(REPORT_INDENT));
    std::cout << "Saved " << (fs::path(folder) / REPORT_FILE).string() << std::endl;
    progress.save(ResearchProgress::FINISHED, STAGE_REPORT, 1, 1, true);
    return 0;
}
