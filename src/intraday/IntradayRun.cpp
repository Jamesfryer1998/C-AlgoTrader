#include "IntradayRun.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include "IntradayBacktest.hpp"
#include "IntradayDay.hpp"
#include "IntradaySettings.hpp"
#include "IntradayUniverse.hpp"
#include "../morning/MorningData.hpp"
#include "../morning/MorningReport.hpp"
#include "../morning/ResearchProgress.hpp"
#include "../pairs/DailyPrices.hpp"
#include "../util/SaveData.hpp"

namespace fs = std::filesystem;

namespace
{
    const std::string INTRADAY_FOLDER = "intraday";
    const std::string REPORT_FILE = "report.json";
    const std::string TITLE = "INTRADAY: STOCKS AGAINST THEIR SECTOR";
    const std::string COUNT_LABEL = "stocks a month";
    const size_t YEAR_MONTH = 7;   // "2026-03" of "2026-03-02"

    const std::string STAGE_TRADING = "Downloading and trading each day";
    const std::string STAGE_REPORT = "Writing the report";
    const int REPORT_INDENT = 1;
}

void
IntradayRun::printUsage(std::ostream& out)
{
    out << "Usage: intraday_app [--holdout] [--offline]\n\n"
        << "Backtests many hedged trades a day on 1-minute bars with the rules in config/intraday.json: stocks that\n"
        << "stray from their sector fund, against random stocks. Saves data/intraday/report.json.\n\n"
        << "  --holdout   also trade the holdout period. Do this once, when the rules are final (it's recorded)\n"
        << "  --offline   don't download: use only what's in data/pairs/ and data/intraday/\n";
}

IntradayRun::IntradayRun(const json& algoConfig, const json& intradayConfig, const std::string& dataRoot)
    : algoConfig(algoConfig), intradayConfig(intradayConfig), dataRoot(dataRoot),
      folder((fs::path(dataRoot) / INTRADAY_FOLDER).string())
{
}

int
IntradayRun::run(const MorningRun::Options& options)
{
    IntradaySettings settings = IntradaySettings::fromConfig(intradayConfig, ResearchProgress::yesterday());
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

    // 1. Daily bars, to pick each month's stocks and their funds
    DailyPrices::Request request{settings.dataStart, settings.end, settings.prefilterDollarVolume,
                                 settings.loadDollarVolume, options.offline, settings.funds};
    PriceMatrix prices;
    size_t downloads = 0;
    if (!DailyPrices::load(algoConfig, dataRoot, request, progress, prices, downloads))
    {
        progress.save(ResearchProgress::FAILED, "", 0, 0, true);
        return 1;
    }

    // 2. Each day: that month's stocks' 1-minute bars, then each variant on its signals and on random stocks
    size_t first = prices.indexOnOrAfter(settings.start);
    size_t last = DailyPrices::indexOnOrBefore(prices, options.holdout ? settings.end : settings.validationEnd);
    json costSettings = algoConfig;
    costSettings["slippage_percent"] = settings.slippagePercent;
    costSettings["starting_capital"] = settings.startingCapital;
    TradingCosts costs(costSettings);
    IntradayBacktest backtest(settings, prices, costs);
    MorningData minutes(algoConfig, folder, options.offline);

    MorningBacktest::Results results;
    results.title = TITLE;
    results.chosen = IntradayBacktest::CHOSEN;
    results.countLabel = COUNT_LABEL;
    results.symbols = static_cast<int>(prices.stocks());
    results.holdoutTraded = options.holdout;
    for (const auto& variant : settings.variants)
        for (const auto& selection : {IntradayBacktest::CHOSEN, MorningBacktest::RANDOM})
        {
            MorningBacktest::Line line;
            line.variant = variant.name;
            line.selection = selection;
            line.equity = settings.startingCapital;
            results.lines.push_back(line);
        }

    IntradayBacktest::Members members;
    std::string month;
    std::vector<IntradayBacktest::Entry> entries;
    for (size_t day = first; day <= last && day < prices.days.size(); day++)
    {
        progress.stage(STAGE_TRADING, day - first, last - first + 1);
        const std::string& date = prices.days[day];
        if (date.substr(0, YEAR_MONTH) != month)
        {
            month = date.substr(0, YEAR_MONTH);
            members = IntradayUniverse::pick(prices, settings, day);
            std::cout << month << ": " << members.size() << " stocks" << std::endl;
        }
        if (members.empty())
            continue;

        std::vector<size_t> rows;
        std::vector<std::string> symbols;
        for (const auto& member : members)
            for (size_t row : {member.stock, member.fund})
                if (std::find(rows.begin(), rows.end(), row) == rows.end())
                {
                    rows.push_back(row);
                    symbols.push_back(prices.symbols[row]);
                }
        IntradayDay bars(minutes.minuteBars(date, symbols), rows, symbols);
        results.daysTraded++;

        for (size_t v = 0; v < settings.variants.size(); v++)
            for (bool control : {false, true})
            {
                MorningBacktest::Line& line = results.lines[2 * v + (control ? 1 : 0)];
                double pnl = backtest.day(settings.variants[v], members, bars, date, line.equity, control, entries, line);
                line.days.push_back(date);
                line.returns.push_back(line.equity > 0 ? pnl / line.equity : 0);
                line.equity += pnl;
            }
    }
    downloads += minutes.downloads();

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
