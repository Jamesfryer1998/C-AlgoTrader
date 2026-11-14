#include "MorningRun.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include "MorningBacktest.hpp"
#include "MorningData.hpp"
#include "MorningReport.hpp"
#include "MorningSettings.hpp"
#include "ResearchProgress.hpp"
#include "../util/MarketTime.hpp"
#include "../util/SaveData.hpp"

namespace fs = std::filesystem;

namespace
{
    const std::string MORNING_FOLDER = "morning";
    const std::string REPORT_FILE = "report.json";
    const std::string TRADES_FILE = "trades.csv";

    const std::string HOLDOUT = "--holdout";
    const std::string OFFLINE = "--offline";
    const std::string HELP = "--help";

    const std::string STAGE_REPORT = "Writing the report";

    const int REPORT_INDENT = 1;

}

MorningRun::Options
MorningRun::parse(const std::vector<std::string>& arguments)
{
    Options options;
    for (const auto& argument : arguments)
    {
        if (argument == HOLDOUT)
            options.holdout = true;
        else if (argument == OFFLINE)
            options.offline = true;
        else if (argument == HELP)
            options.showHelp = true;
        else
            throw std::invalid_argument("Unknown option: " + argument);
    }
    return options;
}

void
MorningRun::printUsage(std::ostream& out)
{
    out << "Usage: morning_app [--holdout] [--offline]\n\n"
        << "Backtests trading each day's stocks in play (highest first-5-minute relative volume) with the rules in\n"
        << "config/morning.json, against the same rules on random stocks. Saves data/morning/report.json.\n\n"
        << "  --holdout   also trade the holdout period. Do this once, when the rules are final (it's recorded)\n"
        << "  --offline   don't download: use only what's in data/morning/\n";
}

MorningRun::MorningRun(const json& algoConfig, const json& morningConfig, const std::string& dataRoot)
    : algoConfig(algoConfig), morningConfig(morningConfig), folder((fs::path(dataRoot) / MORNING_FOLDER).string())
{
}

int
MorningRun::run(const Options& options)
{
    MorningSettings settings = MorningSettings::fromConfig(morningConfig, ResearchProgress::yesterday());
    ResearchProgress progress(folder);
    progress.save(ResearchProgress::RUNNING, "", 0, 0, true);

    std::string holdoutOpened = MorningHoldout::openedAt(folder);
    if (options.holdout)
    {
        std::vector<std::string> variants;
        for (const auto& variant : settings.variants)
            variants.push_back(variant.name);
        std::string first = MorningHoldout::open(folder, variants);
        if (!holdoutOpened.empty())
            std::cout << "NOTE - The holdout was already opened on " << holdoutOpened
                      << ": its result is no longer an unseen test" << std::endl;
        holdoutOpened = first;
    }

    MorningData data(algoConfig, folder, options.offline);
    MorningBacktest backtest(algoConfig, settings);
    MorningBacktest::Results results;
    bool ok = backtest.run(data, options.holdout, [&](const std::string& stage, size_t done, size_t total) {
        progress.stage(stage, done, total);
    }, results);
    if (!ok)
    {
        progress.save(ResearchProgress::FAILED, "", 0, 0, true);
        return 1;
    }

    progress.save(ResearchProgress::RUNNING, STAGE_REPORT, 0, 1, true);
    MorningReport report(settings, results);
    report.print(std::cout);
    SaveData::write((fs::path(folder) / REPORT_FILE).string(),
                    report.toJson(holdoutOpened, data.downloads()).dump(REPORT_INDENT));
    report.writeTrades((fs::path(folder) / TRADES_FILE).string());
    std::cout << "Saved " << (fs::path(folder) / REPORT_FILE).string() << " (" << data.downloads()
              << " requests to Alpaca)" << std::endl;
    progress.save(ResearchProgress::FINISHED, STAGE_REPORT, 1, 1, true);
    return 0;
}
