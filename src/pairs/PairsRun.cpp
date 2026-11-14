#include "PairsRun.hpp"

#include <filesystem>
#include <iostream>
#include "LeadLagBacktest.hpp"
#include "PairsBacktest.hpp"
#include "PairsSettings.hpp"
#include "PriceMatrix.hpp"
#include "../morning/MorningBacktest.hpp"
#include "../morning/MorningData.hpp"
#include "../morning/MorningReport.hpp"
#include "../morning/ResearchProgress.hpp"
#include "../util/SaveData.hpp"

namespace fs = std::filesystem;

namespace
{
    const std::string PAIRS_FOLDER = "pairs";
    const std::string MORNING_FOLDER = "morning";       // the morning screen's downloads, reused where they help
    const std::string SYMBOLS_FILE = "symbols.txt";
    const std::string DAILY_FOLDER = "daily";
    const std::string REPORT_FILE = "report.json";
    const std::string BENCHMARK = "SPY";
    const std::string MATCHED = "matched";
    const std::string TITLE = "PAIRS AND LEAD-LAG";
    const std::string COUNT_LABEL = "pairs a month";

    const std::string STAGE_DAILY = "Daily bars";
    const std::string STAGE_PRICES = "Lining up prices";
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

std::vector<std::string>
PairsRun::worthDownloading(const std::vector<std::string>& symbols, double prefilter) const
{
    fs::path morningDaily = fs::path(dataRoot) / MORNING_FOLDER / DAILY_FOLDER;
    if (!fs::is_directory(morningDaily))
        return symbols;
    std::vector<std::string> kept;
    for (const auto& symbol : symbols)
    {
        auto bars = quant::readDailyCsv((morningDaily / (symbol + ".csv")).string());
        double dollars = 0;
        for (const auto& bar : bars)
            dollars += bar.close * bar.volume;
        if (symbol == BENCHMARK || (!bars.empty() && dollars / bars.size() >= prefilter))
            kept.push_back(symbol);
    }
    return kept;
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

    // 1. Daily bars for the liquid stocks (the morning screen's list of US stocks, if it has one)
    fs::path morningSymbols = fs::path(dataRoot) / MORNING_FOLDER / SYMBOLS_FILE;
    fs::path symbolsFile = fs::path(folder) / SYMBOLS_FILE;
    if (!fs::exists(symbolsFile) && fs::exists(morningSymbols))
        fs::copy_file(morningSymbols, symbolsFile);
    MorningData data(algoConfig, folder, options.offline);
    std::vector<std::string> listed;
    if (!data.symbols(listed))
    {
        std::cerr << "ERROR - Couldn't get the list of US stocks" << std::endl;
        progress.save(ResearchProgress::FAILED, "", 0, 0, true);
        return 1;
    }
    std::vector<std::string> symbols = worthDownloading(listed, settings.prefilterDollarVolume);
    std::cout << symbols.size() << " of " << listed.size() << " stocks trade enough to consider" << std::endl;
    if (!data.ensureDailyBars(symbols, settings.dataStart, settings.end, [&](size_t done, size_t total) {
            progress.stage(STAGE_DAILY, done, total);
        }))
        std::cerr << "WARNING - Some daily bars couldn't be downloaded; those stocks are left out" << std::endl;

    // 2. Every stock's closes on SPY's trading days
    PriceMatrix prices;
    for (const auto& bar : data.dailyBars(BENCHMARK))
        if (bar.date >= settings.dataStart && bar.date <= settings.end)
            prices.days.push_back(bar.date);
    if (prices.days.empty())
    {
        std::cerr << "ERROR - No daily bars for " << BENCHMARK << ", the trading calendar" << std::endl;
        progress.save(ResearchProgress::FAILED, "", 0, 0, true);
        return 1;
    }
    for (size_t i = 0; i < symbols.size(); i++)
    {
        progress.stage(STAGE_PRICES, i, symbols.size());
        prices.add(symbols[i], data.dailyBars(symbols[i]));
    }

    // 3. Each rule on matched and on random pairs, from the start to the end of validation (or the holdout)
    size_t first = prices.indexOnOrAfter(settings.start);
    std::string lastDate = options.holdout ? settings.end : settings.validationEnd;
    size_t last = prices.indexOnOrAfter(lastDate);
    if (last >= prices.days.size() || prices.days[last] > lastDate)
        last = last == 0 ? 0 : last - 1;
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
    results.symbols = static_cast<int>(symbols.size());
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

    // 4. SPY bought and held, for comparison
    MorningBacktest::Line spy;
    spy.variant = MorningBacktest::BENCHMARK;
    spy.selection = BENCHMARK;
    spy.equity = settings.startingCapital;
    size_t spyRow = prices.stocks();
    for (size_t stock = 0; stock < prices.stocks(); stock++)
        if (prices.symbols[stock] == BENCHMARK)
            spyRow = stock;
    for (size_t day = first; spyRow < prices.stocks() && day <= last && day < prices.days.size(); day++)
    {
        double change = prices.dailyReturn(spyRow, day);
        spy.days.push_back(prices.days[day]);
        spy.returns.push_back(change);
        spy.equity *= 1 + change;
    }
    results.lines.push_back(spy);

    // 5. The verdicts
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
