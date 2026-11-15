#include "PairsRun.hpp"

#include <filesystem>
#include <iostream>
#include "DailyPrices.hpp"
#include "LeadLagBacktest.hpp"
#include "PairsBacktest.hpp"
#include "PairsSettings.hpp"
#include "PriceMatrix.hpp"
#include "../morning/MorningBacktest.hpp"
#include "../morning/MorningReport.hpp"
#include "../morning/ResearchProgress.hpp"
#include "../util/SaveData.hpp"

namespace fs = std::filesystem;

namespace
{
    const std::string PAIRS_FOLDER = "pairs";
    const std::string REPORT_FILE = "report.json";
    const std::string MATCHED = "matched";
    const std::string TITLE = "PAIRS AND LEAD-LAG";
    const std::string COUNT_LABEL = "pairs a month";

    const std::string STAGE_TRADING = "Trading each rule";
    const std::string STAGE_REPORT = "Writing the report";
    const int REPORT_INDENT = 1;
}

void
PairsRun::printUsage(std::ostream& out)
{
    out << "Usage: pairs_app [--holdout] [--offline]\n\n"
        << "Backtests pairs trading and lead-lag on daily bars with the rules in config/pairs.json, against random\n"
        << "pairs and leaders. Saves data/pairs/report.json.\n\n"
        << "  --holdout   also trade the holdout period. Do this once, when the rules are final (it's recorded)\n"
        << "  --offline   don't download: use only what's in data/pairs/\n";
}

PairsRun::PairsRun(const json& algoConfig, const json& pairsConfig, const std::string& dataRoot)
    : algoConfig(algoConfig), pairsConfig(pairsConfig), dataRoot(dataRoot),
      folder((fs::path(dataRoot) / PAIRS_FOLDER).string())
{
}

int
PairsRun::run(const MorningRun::Options& options)
{
    PairsSettings settings = PairsSettings::fromConfig(pairsConfig, ResearchProgress::yesterday());
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
    DailyPrices::Request request{settings.dataStart, settings.end, settings.prefilterDollarVolume, 0, options.offline, {}};
    PriceMatrix prices;
    size_t downloads = 0;
    if (!DailyPrices::load(algoConfig, dataRoot, request, progress, prices, downloads))
    {
        progress.save(ResearchProgress::FAILED, "", 0, 0, true);
        return 1;
    }

    // 2. Each rule on matched and on random pairs, from the start to the end of validation (or the holdout)
    size_t first = prices.indexOnOrAfter(settings.start);
    std::string lastDate = options.holdout ? settings.end : settings.validationEnd;
    size_t last = DailyPrices::indexOnOrBefore(prices, lastDate);
    json costSettings = algoConfig;
    costSettings["slippage_percent"] = settings.slippagePercent;
    costSettings["starting_capital"] = settings.startingCapital;
    TradingCosts costs(costSettings);
    PairsBacktest pairs(settings, prices, costs);
    LeadLagBacktest leadLag(settings, prices, costs);

    MorningBacktest::Results results;
    results.title = TITLE;
    results.chosen = MATCHED;
    results.countLabel = COUNT_LABEL;
    results.symbols = static_cast<int>(prices.stocks());
    results.daysTraded = first <= last && first < prices.days.size() ? static_cast<int>(last - first + 1) : 0;
    results.holdoutTraded = options.holdout;
    for (size_t v = 0; v < settings.variants.size(); v++)
    {
        const auto& variant = settings.variants[v];
        progress.stage(STAGE_TRADING, v, settings.variants.size());
        for (bool matched : {true, false})
        {
            MorningBacktest::Line line;
            line.variant = variant.name;
            line.selection = matched ? MATCHED : MorningBacktest::RANDOM;
            line.equity = settings.startingCapital;
            if (variant.kind == PairsSettings::Kind::Pairs)
                pairs.run(variant, matched, first, last, line);
            else
                leadLag.run(variant, matched, first, last, line);
            results.lines.push_back(line);
        }
    }

    // 3. SPY bought and held, for comparison
    results.lines.push_back(DailyPrices::buyAndHold(prices, first, last, settings.startingCapital));

    // 4. The verdicts
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
