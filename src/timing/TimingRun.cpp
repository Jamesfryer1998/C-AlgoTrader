#include "TimingRun.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include "TimingBacktest.hpp"
#include "TimingRule.hpp"
#include "TimingSeries.hpp"
#include "TimingSettings.hpp"
#include "../morning/MorningData.hpp"
#include "../morning/MorningReport.hpp"
#include "../morning/ResearchProgress.hpp"
#include "../morning/StockSelection.hpp"
#include "../quant/DeflatedSharpe.hpp"
#include "../util/SaveData.hpp"

namespace fs = std::filesystem;

namespace
{
    const std::string TIMING_FOLDER = "timing";
    const std::string REPORT_FILE = "report.json";
    const std::string RULES_FILE = "rules.csv";
    const std::string TITLE = "MARKET TIMING SEARCH";
    const std::string CHOSEN = "timed";
    const std::string COUNT_LABEL = "symbol timed";

    const std::string STAGE_DATA = "Daily bars";
    const std::string STAGE_RULES = "Trading every rule";
    const std::string STAGE_FINALISTS = "Finalists against random timings";
    const std::string STAGE_REPORT = "Writing the report";
    const int REPORT_INDENT = 1;
    const int PRINTED_RULES = 25;
    const int PERCENT_DECIMALS = 2;
    const double PERCENT = 100.0;

    struct Candidate
    {
        TimingRule rule;
        std::map<std::string, TimingBacktest::Measures> periods;
        double developmentDailySharpe = 0;
        bool eligible = false;
        bool finalist = false;
        std::map<std::string, double> randomPercentile;   // finalists: the share of random timings it beat
    };

    double median(std::vector<double> values)
    {
        if (values.empty())
            return 0;
        std::sort(values.begin(), values.end());
        size_t middle = values.size() / 2;
        return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
    }

    json measuresJson(const TimingBacktest::Measures& m)
    {
        return {{"cagr", m.cagr}, {"sharpe", m.sharpe}, {"max_drawdown", m.maxDrawdown}, {"exposure", m.exposure},
                {"trades", m.trades}, {"mix_cagr", m.mixCagr}, {"edge", m.edge}, {"capture", m.capture},
                {"days", m.days}};
    }
}

void
TimingRun::printUsage(std::ostream& out)
{
    out << "Usage: timing_app [--holdout] [--offline]\n\n"
        << "Searches thousands of rules for when to hold SPY (config/timing.json), ranks them on the development\n"
        << "years by what their timing added over holding as much without timing, and tests the finalists on\n"
        << "validation against random timings. Saves data/timing/report.json and rules.csv.\n\n"
        << "  --holdout   also trade the finalists through the holdout. Do this once, when the rules are final\n"
        << "  --offline   don't download: use only what's in data/timing/\n";
}

TimingRun::TimingRun(const json& algoConfig, const json& timingConfig, const std::string& dataRoot)
    : algoConfig(algoConfig), timingConfig(timingConfig), dataRoot(dataRoot),
      folder((fs::path(dataRoot) / TIMING_FOLDER).string())
{
}

int
TimingRun::run(const MorningRun::Options& options)
{
    TimingSettings settings = TimingSettings::fromConfig(timingConfig, ResearchProgress::yesterday());
    fs::create_directories(folder);
    ResearchProgress progress(folder);
    progress.save(ResearchProgress::RUNNING, STAGE_DATA, 0, 1, true);
    std::string holdoutOpened = MorningHoldout::openedAt(folder);

    // 1. The symbol's and the cash fund's daily bars, dividends in
    json dataConfig = algoConfig;
    dataConfig["alpaca"]["history_adjustment"] = settings.adjustment;
    MorningData data(dataConfig, folder, options.offline);
    std::vector<std::string> symbols = {settings.symbol};
    if (!settings.cashSymbol.empty())
        symbols.push_back(settings.cashSymbol);
    if (!data.ensureDailyBars(symbols, settings.dataStart, settings.end, [&](size_t done, size_t total) {
            progress.stage(STAGE_DATA, done, total);
        }))
        std::cerr << "WARNING - Some daily bars couldn't be downloaded" << std::endl;
    std::vector<quant::DailyBar> bars;
    for (const auto& bar : data.dailyBars(settings.symbol))
        if (bar.date >= settings.dataStart && bar.date <= settings.end)
            bars.push_back(bar);
    if (bars.size() < 2)
    {
        std::cerr << "ERROR - No daily bars for " << settings.symbol << std::endl;
        progress.save(ResearchProgress::FAILED, "", 0, 0, true);
        return 1;
    }
    TimingSeries series(bars, settings.cashSymbol.empty() ? std::vector<quant::DailyBar>()
                                                          : data.dailyBars(settings.cashSymbol));
    std::cout << settings.symbol << ": " << series.days.front() << " to " << series.days.back() << std::endl;

    size_t first = static_cast<size_t>(std::lower_bound(series.days.begin(), series.days.end(), settings.start)
                                       - series.days.begin());
    std::string lastDate = options.holdout ? settings.end : settings.validationEnd;
    size_t last = static_cast<size_t>(std::upper_bound(series.days.begin(), series.days.end(), lastDate)
                                      - series.days.begin());
    last = last == 0 ? 0 : last - 1;
    TimingBacktest backtest(settings, series);
    MorningSettings reportSettings = settings.forReport();
    std::vector<std::string> periods = {MorningSettings::DEVELOPMENT, MorningSettings::VALIDATION};
    if (options.holdout)
        periods.push_back(MorningSettings::HOLDOUT);

    // 2. Every rule, through validation (or the holdout)
    std::vector<TimingRule> rules = TimingRule::expand(settings.families);
    std::cout << rules.size() << " rules" << std::endl;
    std::vector<Candidate> candidates;
    std::vector<std::vector<double>> targets;   // kept for the finalists' random timings
    for (size_t r = 0; r < rules.size(); r++)
    {
        progress.stage(STAGE_RULES, r, rules.size());
        Candidate candidate;
        candidate.rule = rules[r];
        std::vector<double> ruleTargets = rules[r].targets(series);
        TimingBacktest::Traded traded = backtest.trade(ruleTargets, first, last);
        for (const auto& period : periods)
            candidate.periods[period] = backtest.measure(traded, period);
        std::vector<double> developmentReturns;
        for (size_t i = 0; i < traded.days.size(); i++)
            if (reportSettings.periodOf(traded.days[i]) == MorningSettings::DEVELOPMENT)
                developmentReturns.push_back(traded.returns[i]);
        candidate.developmentDailySharpe = quant::dailySharpe(developmentReturns);
        const auto& development = candidate.periods[MorningSettings::DEVELOPMENT];
        candidate.eligible = development.exposure >= settings.minExposure && development.exposure <= settings.maxExposure
                          && development.trades >= settings.minTrades;
        candidates.push_back(candidate);
        targets.push_back(std::move(ruleTargets));
    }

    // 3. The finalists: the most added by timing in development, a few from each family
    std::vector<size_t> ranked;
    for (size_t c = 0; c < candidates.size(); c++)
        if (candidates[c].eligible)
            ranked.push_back(c);
    auto developmentEdge = [&](size_t c) { return candidates[c].periods[MorningSettings::DEVELOPMENT].edge; };
    std::stable_sort(ranked.begin(), ranked.end(), [&](size_t a, size_t b) { return developmentEdge(a) > developmentEdge(b); });
    std::vector<size_t> finalists;
    std::map<std::string, size_t> perFamily;
    for (size_t c : ranked)
        if (finalists.size() < settings.finalists && perFamily[candidates[c].rule.family] < settings.perFamily)
        {
            perFamily[candidates[c].rule.family]++;
            candidates[c].finalist = true;
            finalists.push_back(c);
        }
    if (options.holdout)
    {
        std::vector<std::string> names;
        for (size_t c : finalists)
            names.push_back(candidates[c].rule.name());
        holdoutOpened = MorningHoldout::open(folder, names);
    }

    // 4. Each finalist against random timings of its own stretches in and out
    MorningBacktest::Results results;
    results.title = TITLE;
    results.chosen = CHOSEN;
    results.countLabel = COUNT_LABEL;
    results.symbols = 1;
    results.daysTraded = first <= last ? static_cast<int>(last - first + 1) : 0;
    results.holdoutTraded = options.holdout;
    for (size_t f = 0; f < finalists.size(); f++)
    {
        progress.stage(STAGE_FINALISTS, f, finalists.size());
        Candidate& candidate = candidates[finalists[f]];
        std::string name = candidate.rule.name();
        std::mt19937_64 random(StockSelection::stableHash(name) ^ settings.randomSeed);
        TimingBacktest::Traded timed = backtest.trade(targets[finalists[f]], first, last);
        TimingBacktest::Traded firstRandom;
        std::map<std::string, int> beaten;
        for (int draw = 0; draw < settings.randomDraws; draw++)
        {
            TimingBacktest::Traded shuffled = backtest.trade(backtest.shuffled(targets[finalists[f]], first, last, random),
                                                             first, last);
            for (const auto& period : periods)
                if (candidate.periods[period].cagr > backtest.measure(shuffled, period).cagr)
                    beaten[period]++;
            if (draw == 0)
                firstRandom = shuffled;
        }
        for (const auto& period : periods)
            candidate.randomPercentile[period] = settings.randomDraws > 0 ? static_cast<double>(beaten[period]) / settings.randomDraws : 0;
        results.lines.push_back(backtest.line(timed, name, CHOSEN));
        results.lines.push_back(backtest.line(firstRandom, name, MorningBacktest::RANDOM));
        MorningSettings::Variant variant;
        variant.name = name;
        variant.step = candidate.rule.family;
        reportSettings.variants.push_back(variant);
    }
    MorningBacktest::Line hold = backtest.line(backtest.buyAndHold(first, last), MorningBacktest::BENCHMARK, settings.symbol);
    results.lines.push_back(hold);
    for (const auto& c : ranked)
        results.trialSharpes[MorningSettings::DEVELOPMENT].push_back(candidates[c].developmentDailySharpe);

    // 5. The leaderboard: every eligible rule ranked on development; validation only for the finalists, so a
    //    rule can't be picked for how it did there. By family, the medians are over all of them.
    progress.save(ResearchProgress::RUNNING, STAGE_REPORT, 0, 1, true);
    TimingBacktest::Traded held = backtest.buyAndHold(first, last);
    json leaderboard = {{"rules_tried", candidates.size()}, {"eligible", ranked.size()}, {"symbol", settings.symbol},
                        {"cash", settings.cashSymbol}};
    for (const auto& period : periods)
        leaderboard["buy_and_hold"][period] = measuresJson(backtest.measure(held, period));
    std::vector<double> developmentEdges, validationEdges;
    int developmentAhead = 0, validationAhead = 0;
    for (size_t c : ranked)
    {
        double development = candidates[c].periods[MorningSettings::DEVELOPMENT].edge;
        double validation = candidates[c].periods[MorningSettings::VALIDATION].edge;
        developmentEdges.push_back(development);
        validationEdges.push_back(validation);
        developmentAhead += development > 0 ? 1 : 0;
        validationAhead += validation > 0 ? 1 : 0;
    }
    double share = ranked.empty() ? 0 : 1.0 / ranked.size();
    leaderboard["ahead_of_mix"] = {{"development", developmentAhead * share}, {"validation", validationAhead * share}};
    leaderboard["edge_rank_correlation"] = TimingBacktest::spearman(developmentEdges, validationEdges);

    json families = json::array();
    for (const auto& family : TimingRule::familyNames())
    {
        std::vector<double> development, validation;
        size_t tried = 0;
        for (const auto& candidate : candidates)
            if (candidate.rule.family == family)
            {
                tried++;
                if (!candidate.eligible)
                    continue;
                development.push_back(candidate.periods.at(MorningSettings::DEVELOPMENT).edge);
                validation.push_back(candidate.periods.at(MorningSettings::VALIDATION).edge);
            }
        if (tried == 0)
            continue;
        size_t validationAheadCount = std::count_if(validation.begin(), validation.end(), [](double edge) { return edge > 0; });
        families.push_back({{"family", family}, {"tried", tried}, {"eligible", development.size()},
                            {"best_development_edge", development.empty() ? 0 : *std::max_element(development.begin(), development.end())},
                            {"median_development_edge", median(development)},
                            {"median_validation_edge", median(validation)},
                            {"validation_ahead", validation.empty() ? 0 : static_cast<double>(validationAheadCount) / validation.size()}});
    }
    leaderboard["families"] = families;

    json top = json::array();
    for (size_t i = 0; i < ranked.size() && i < settings.leaderboardSize; i++)
    {
        const Candidate& candidate = candidates[ranked[i]];
        json row = {{"rank", i + 1}, {"rule", candidate.rule.name()}, {"family", candidate.rule.family},
                    {"finalist", candidate.finalist},
                    {"development", measuresJson(candidate.periods.at(MorningSettings::DEVELOPMENT))}};
        if (candidate.finalist)
            for (const auto& period : periods)
            {
                if (period != MorningSettings::DEVELOPMENT)
                    row[period] = measuresJson(candidate.periods.at(period));
                row["random_percentile"][period] = candidate.randomPercentile.at(period);
            }
        top.push_back(row);
    }
    leaderboard["top"] = top;

    // 6. The verdicts
    MorningReport report(reportSettings, results);
    report.print(std::cout);
    json saved = report.toJson(holdoutOpened, data.downloads());
    saved["leaderboard"] = leaderboard;
    SaveData::write((fs::path(folder) / REPORT_FILE).string(), saved.dump(REPORT_INDENT));

    std::ostringstream csv;
    csv << "rank,rule,family,eligible,finalist,exposure,trades,cagr,mix_cagr,edge,capture,sharpe,max_drawdown\n";
    std::vector<size_t> everyRule(candidates.size());
    for (size_t c = 0; c < candidates.size(); c++)
        everyRule[c] = c;
    std::stable_sort(everyRule.begin(), everyRule.end(), [&](size_t a, size_t b) { return developmentEdge(a) > developmentEdge(b); });
    for (size_t i = 0; i < everyRule.size(); i++)
    {
        const Candidate& candidate = candidates[everyRule[i]];
        const auto& m = candidate.periods.at(MorningSettings::DEVELOPMENT);
        csv << i + 1 << ",\"" << candidate.rule.name() << "\"," << candidate.rule.family << "," << candidate.eligible
            << "," << candidate.finalist << "," << m.exposure << "," << m.trades << "," << m.cagr << "," << m.mixCagr
            << "," << m.edge << "," << m.capture << "," << m.sharpe << "," << m.maxDrawdown << "\n";
    }
    SaveData::write((fs::path(folder) / RULES_FILE).string(), csv.str());

    std::cout << "\n" << candidates.size() << " rules, " << ranked.size() << " eligible. Ahead of the exposure-matched mix: "
              << std::fixed << std::setprecision(0) << developmentAhead * share * PERCENT << "% in development, "
              << validationAhead * share * PERCENT << "% in validation. Development edge vs validation edge rank correlation "
              << std::setprecision(PERCENT_DECIMALS) << leaderboard["edge_rank_correlation"].get<double>() << "\n";
    std::cout << "\nTOP " << PRINTED_RULES << " ON DEVELOPMENT (edge = CAGR - the exposure-matched mix's)\n";
    for (size_t i = 0; i < ranked.size() && i < static_cast<size_t>(PRINTED_RULES); i++)
    {
        const Candidate& candidate = candidates[ranked[i]];
        const auto& d = candidate.periods.at(MorningSettings::DEVELOPMENT);
        std::cout << std::setw(3) << i + 1 << (candidate.finalist ? " * " : "   ") << std::left << std::setw(70)
                  << candidate.rule.name() << std::right << " exposure " << std::setw(5) << d.exposure * PERCENT
                  << "%  edge " << std::setw(6) << d.edge * PERCENT << "%";
        if (candidate.finalist)
        {
            const auto& v = candidate.periods.at(MorningSettings::VALIDATION);
            std::cout << "  | validation edge " << std::setw(6) << v.edge * PERCENT << "%, beat "
                      << std::setprecision(0) << candidate.randomPercentile.at(MorningSettings::VALIDATION) * PERCENT
                      << "% of random" << std::setprecision(PERCENT_DECIMALS);
        }
        std::cout << "\n";
    }
    std::cout << "Saved " << (fs::path(folder) / REPORT_FILE).string() << " and " << RULES_FILE << " ("
              << data.downloads() << " requests to Alpaca)" << std::endl;
    progress.save(ResearchProgress::FINISHED, STAGE_REPORT, 1, 1, true);
    return 0;
}
