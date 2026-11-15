#include "SwingRun.hpp"

#include <filesystem>
#include <iostream>
#include "SwingBacktest.hpp"
#include "SwingIndicators.hpp"
#include "SwingSettings.hpp"
#include "../morning/MorningReport.hpp"
#include "../morning/ResearchProgress.hpp"
#include "../pairs/DailyPrices.hpp"
#include "../util/SaveData.hpp"

namespace fs = std::filesystem;

namespace
{
    const std::string SWING_FOLDER = "swing";
    const std::string REPORT_FILE = "report.json";
    const std::string TITLE = "SWING TRADING";
    const std::string COUNT_LABEL = "positions at once";

    const std::string STAGE_INDICATORS = "Working out the signals";
    const std::string STAGE_TRADING = "Trading each rule";
    const std::string STAGE_REPORT = "Writing the report";
    const int REPORT_INDENT = 1;
}

void
SwingRun::printUsage(std::ostream& out)
{
    out << "Usage: swing_app [--holdout] [--offline]\n\n"
        << "Backtests swing trades (held days to weeks) on daily bars with the rules in config/swing.json, against\n"
        << "random buys. Saves data/swing/report.json.\n\n"
        << "  --holdout   also trade the holdout period. Do this once, when the rules are final (it's recorded)\n"
        << "  --offline   don't download: use only what's in data/pairs/\n";
}

SwingRun::SwingRun(const json& algoConfig, const json& swingConfig, const std::string& dataRoot)
    : algoConfig(algoConfig), swingConfig(swingConfig), dataRoot(dataRoot),
      folder((fs::path(dataRoot) / SWING_FOLDER).string())
{
}

int
SwingRun::run(const MorningRun::Options& options)
{
    SwingSettings settings = SwingSettings::fromConfig(swingConfig, ResearchProgress::yesterday());
    fs::create_directories(folder);
    ResearchProgress progress(folder);
    progress.save(ResearchProgress::RUNNING, "", 0, 0, true);

    std::string holdoutOpened = MorningHoldout::openedAt(folder);
    if (options.holdout)
    {
        std::vector<std::string> variants;
        for (const auto& variant : settings.variants)
            variants.push_back(variant.name);
        holdoutOpened = MorningHoldout::open(folder, variants);
    }

    // 1. Daily bars for the liquid stocks, on SPY's trading days
    DailyPrices::Request request{settings.dataStart, settings.end, settings.prefilterDollarVolume,
                                 settings.loadDollarVolume, options.offline};
    PriceMatrix prices;
    size_t downloads = 0;
    if (!DailyPrices::load(algoConfig, dataRoot, request, progress, prices, downloads))
    {
        progress.save(ResearchProgress::FAILED, "", 0, 0, true);
        return 1;
    }
    std::cout << prices.stocks() << " stocks loaded" << std::endl;

    // 2. What the signals look at, every stock every day
    progress.save(ResearchProgress::RUNNING, STAGE_INDICATORS, 0, 1, true);
    SwingIndicators indicators(prices, settings);

    // 3. Each rule on its own signals and on random buys, from the start to the end of validation (or the holdout)
    size_t first = prices.indexOnOrAfter(settings.start);
    size_t last = DailyPrices::indexOnOrBefore(prices, options.holdout ? settings.end : settings.validationEnd);
    json costSettings = algoConfig;
    costSettings["slippage_percent"] = settings.slippagePercent;
    costSettings["starting_capital"] = settings.startingCapital;
    TradingCosts costs(costSettings);
    SwingBacktest backtest(settings, prices, indicators, costs);

    MorningBacktest::Results results;
    results.title = TITLE;
    results.chosen = SwingBacktest::CHOSEN;
    results.countLabel = COUNT_LABEL;
    results.symbols = static_cast<int>(prices.stocks());
    results.daysTraded = first <= last && first < prices.days.size() ? static_cast<int>(last - first + 1) : 0;
    results.holdoutTraded = options.holdout;
    for (size_t v = 0; v < settings.variants.size(); v++)
    {
        const auto& variant = settings.variants[v];
        progress.stage(STAGE_TRADING, v, settings.variants.size());
        for (bool chosen : {true, false})
        {
            MorningBacktest::Line line;
            line.variant = variant.name;
            line.selection = chosen ? SwingBacktest::CHOSEN : MorningBacktest::RANDOM;
            line.equity = settings.startingCapital;
            if (first < prices.days.size())
                backtest.run(variant, chosen, first, last, line);
            results.lines.push_back(line);
        }
    }

    // 4. SPY bought and held, for comparison
    results.lines.push_back(DailyPrices::buyAndHold(prices, first, last, settings.startingCapital));

    // 5. The verdicts
    progress.save(ResearchProgress::RUNNING, STAGE_REPORT, 0, 1, true);
    MorningReport report(settings.forReport(), results);
    report.print(std::cout);
    SaveData::write((fs::path(folder) / REPORT_FILE).string(),
                    report.toJson(holdoutOpened, downloads).dump(REPORT_INDENT));
    std::cout << "Saved " << (fs::path(folder) / REPORT_FILE).string() << " (" << downloads
              << " requests to Alpaca)" << std::endl;
    progress.save(ResearchProgress::FINISHED, STAGE_REPORT, 1, 1, true);
    return 0;
}
