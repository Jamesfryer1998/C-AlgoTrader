#include "RiskTuning.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include "RiskComparison.hpp"
#include "../risk/FixedSizeRisk.hpp"
#include "../util/Format.hpp"
#include "../util/MarketTime.hpp"
#include "../util/TextTable.hpp"

namespace
{
    const std::string TUNING = "risk_tuning";
    const std::string GRIDS = "grids";
    const std::string NAME = "name";

    const double DEFAULT_MAX_DRAWDOWN_PERCENT = 10.0;
    const size_t DEFAULT_FINALISTS = 3;
    const double PERCENT = 100.0;
    const int TEST_DAYS_DIVISOR = 2;   // default test period: half as many days as training

    // Grids for engines that "risk_tuning" doesn't list. RISK_PER_TRADE's values are small because the
    // strategies' stops are about 1% away: risking 0.2% of equity there is already a 20% position.
    const json DEFAULT_GRIDS = {
        {"FIXED_FRACTION", {{"position_percent", {5, 10, 15, 20, 30}}, {"max_stock_percent", {20, 40}}}},
        {"RISK_PER_TRADE", {{"risk_percent", {0.05, 0.1, 0.15, 0.2, 0.3}}, {"max_stock_percent", {20, 40}}}},
        {"VOLATILITY_TARGET", {{"daily_volatility_percent", {0.1, 0.25, 0.5, 1.0}}, {"volatility_bars", {30, 60, 120}},
                               {"max_stock_percent", {20, 40}}}},
    };

    // The baseline every setting is shown against: one share per order, as the app traded before
    const json BASELINE = {{NAME, FixedSizeRisk::NAME}, {"quantity", 1}};

    // Every combination of a grid's values, e.g. {a: [1, 2], b: [3]} -> {a: 1, b: 3}, {a: 2, b: 3}
    std::vector<json> combinations(const std::string& engine, const json& grid)
    {
        std::vector<json> result = {json{{NAME, engine}}};
        for (const auto& [key, values] : grid.items())
        {
            std::vector<json> next;
            json list = values.is_array() ? values : json::array({values});
            for (const auto& partial : result)
                for (const auto& value : list)
                {
                    json withValue = partial;
                    withValue[key] = value;
                    next.push_back(withValue);
                }
            result = next;
        }
        return result;
    }

    double weakerReturn(double a, double b) { return std::min(a, b); }

    // Settings that traded exactly alike (a cap was binding, so the extra size never got used)
    bool sameOutcome(const LiveTrader::Result& a, const LiveTrader::Result& b)
    {
        return a.endingEquity == b.endingEquity && a.fills == b.fills && a.mostCapitalInUse == b.mostCapitalInUse;
    }

    // "RISK_PER_TRADE risk 0.5% stock<=20%": the engine and the grid's settings, short
    std::string describeSettings(const json& risk)
    {
        std::string text = risk.value(NAME, std::string());
        for (const auto& [key, value] : risk.items())
            if (key != NAME)
                text += " " + key + "=" + value.dump();
        return text;
    }
}

RiskTuning::RiskTuning(const json& config, const std::string& dataRoot, const std::string& configPath,
                       const RiskOptions& options)
    : config(config), dataRoot(dataRoot), configPath(configPath), options(options)
{
    json tuning = config.value(TUNING, json::object());
    double limitPercent = options.maxDrawdownPercent != RiskOptions::NOT_GIVEN
                        ? options.maxDrawdownPercent
                        : tuning.value("max_drawdown_percent", DEFAULT_MAX_DRAWDOWN_PERCENT);
    maxDrawdown = limitPercent / PERCENT;
    finalists = tuning.value("finalists", DEFAULT_FINALISTS);
    testDays = options.testDays != RiskOptions::NOT_GIVEN ? options.testDays
                                                          : std::max(1, options.replay.replayDays / TEST_DAYS_DIVISOR);
}

std::vector<json>
RiskTuning::grid(const json& config, const std::vector<std::string>& engines)
{
    json grids = DEFAULT_GRIDS;
    json tuning = config.value(TUNING, json::object());
    json configured = tuning.value(GRIDS, json::object());
    for (const auto& [engine, settings] : configured.items())
        grids[engine] = settings;

    std::vector<json> blocks;
    for (const auto& [engine, settings] : grids.items())
    {
        if (!engines.empty() && std::find(engines.begin(), engines.end(), engine) == engines.end())
            continue;
        for (const auto& block : combinations(engine, settings))
            blocks.push_back(block);
    }
    return blocks;
}

int
RiskTuning::run(std::ostream& out)
{
    int trainDays = options.replay.replayDays;

    std::vector<Candidate> candidates;
    candidates.push_back({BASELINE, RiskComparison::label(config, BASELINE), {}, {}});
    for (const auto& block : grid(config, options.engines))
        candidates.push_back({block, describeSettings(block), {}, {}});

    // 1. Training
    out << "Training: " << candidates.size() << " settings x " << trainDays << " day(s) from "
        << options.replay.replayDay << std::endl;
    for (size_t i = 0; i < candidates.size(); i++)
    {
        out << "  [" << i + 1 << "/" << candidates.size() << "] " << candidates[i].name << "..." << std::flush;
        if (!replay(candidates[i], false, out))
            return 1;
    }
    Candidate baseline = candidates.front();

    // 2. Shortlist: within the drawdown limit, most made first; of settings that traded exactly alike, only the
    //    first in the grid (the smallest) is kept
    std::vector<Candidate> ranked;
    for (size_t i = 1; i < candidates.size(); i++)
        if (withinLimit(candidates[i].train))
            ranked.push_back(candidates[i]);
    std::stable_sort(ranked.begin(), ranked.end(), [](const Candidate& a, const Candidate& b) {
        return RiskComparison::returnOf(a.train) > RiskComparison::returnOf(b.train);
    });
    std::vector<Candidate> shortlist;
    for (const auto& candidate : ranked)
    {
        bool duplicate = std::any_of(shortlist.begin(), shortlist.end(),
                                     [&](const Candidate& kept) { return sameOutcome(kept.train, candidate.train); });
        if (!duplicate && shortlist.size() < finalists)
            shortlist.push_back(candidate);
    }
    if (shortlist.empty())
    {
        out << "\nNo setting stayed within a " << Format::percent(maxDrawdown, 0)
            << " drawdown on the training days. Try a larger --max-drawdown or smaller sizes in \"risk_tuning\".\n";
        return 1;
    }

    // 3. Test, on the days after training
    testFirstDay = MarketTime::addWeekdays(baseline.train.lastDay, 1);
    out << "\nTest: the best " << shortlist.size() << " on " << testDays << " day(s) from " << testFirstDay << std::endl;
    out << "  " << baseline.name << "..." << std::flush;
    if (!replay(baseline, true, out))
        return 1;
    for (auto& candidate : shortlist)
    {
        out << "  " << candidate.name << "..." << std::flush;
        if (!replay(candidate, true, out))
            return 1;
    }

    std::vector<Candidate> everything(candidates.begin(), candidates.end());
    std::stable_sort(everything.begin() + 1, everything.end(), [](const Candidate& a, const Candidate& b) {
        return RiskComparison::returnOf(a.train) > RiskComparison::returnOf(b.train);
    });
    out << "\n=== Training: " << baseline.train.firstDay << " to " << baseline.train.lastDay << " ===\n";
    printTable(out, everything, false);
    std::vector<Candidate> finalRows = {baseline};
    finalRows.insert(finalRows.end(), shortlist.begin(), shortlist.end());
    out << "\n=== Shortlist on the test days: " << baseline.test.firstDay << " to " << baseline.test.lastDay << " ===\n";
    printTable(out, finalRows, true);

    // 4. Pick: best weaker return of the two periods, within the limit on test too
    const Candidate* chosen = nullptr;
    for (const auto& candidate : shortlist)
    {
        if (!withinLimit(candidate.test))
            continue;
        double score = weakerReturn(RiskComparison::returnOf(candidate.train), RiskComparison::returnOf(candidate.test));
        if (!chosen || score > weakerReturn(RiskComparison::returnOf(chosen->train), RiskComparison::returnOf(chosen->test)))
            chosen = &candidate;
    }
    if (!chosen)
    {
        out << "\nNone of the shortlist stayed within the " << Format::percent(maxDrawdown, 0)
            << " drawdown on the test days, so nothing was chosen.\n";
        return 1;
    }

    out << "\nChosen: " << chosen->name << "\n  training " << Format::percentChange(RiskComparison::returnOf(chosen->train))
        << " (drawdown " << Format::percent(chosen->train.maxDrawdown) << "), test "
        << Format::percentChange(RiskComparison::returnOf(chosen->test)) << " (drawdown "
        << Format::percent(chosen->test.maxDrawdown) << "); one share per order made "
        << Format::percentChange(RiskComparison::returnOf(baseline.train)) << " and "
        << Format::percentChange(RiskComparison::returnOf(baseline.test)) << ".\n"
        << "  \"risk\": " << oneLine(chosen->risk) << "\n";

    if (!options.save)
    {
        out << "Run again with --save to put it in config.json.\n";
        return 0;
    }
    std::ostringstream note;
    note << "Chosen by risk_app --tune: training " << baseline.train.firstDay << " to " << baseline.train.lastDay << " "
         << Format::percentChange(RiskComparison::returnOf(chosen->train)) << " (drawdown "
         << Format::percent(chosen->train.maxDrawdown) << "), test " << baseline.test.firstDay << " to "
         << baseline.test.lastDay << " " << Format::percentChange(RiskComparison::returnOf(chosen->test)) << " (drawdown "
         << Format::percent(chosen->test.maxDrawdown) << "), drawdown limit " << Format::percent(maxDrawdown, 0);
    if (!saveToConfig(configPath, chosen->risk, note.str()))
    {
        out << "ERROR - Could not update " << configPath << "\n";
        return 1;
    }
    out << "Saved to " << configPath << "\n";
    return 0;
}

bool
RiskTuning::withinLimit(const LiveTrader::Result& result) const
{
    return result.maxDrawdown <= maxDrawdown;
}

bool
RiskTuning::replay(Candidate& candidate, bool testPeriod, std::ostream& out)
{
    bool ok = false;
    std::string firstDay = testPeriod ? testFirstDay : options.replay.replayDay;
    int days = testPeriod ? testDays : options.replay.replayDays;
    LiveTrader::Result result = RiskComparison::replayWith(config, dataRoot, candidate.risk, firstDay, days, ok);
    if (!ok)
    {
        out << " replay failed" << std::endl;
        return false;
    }
    (testPeriod ? candidate.test : candidate.train) = result;
    out << " " << Format::percentChange(RiskComparison::returnOf(result)) << ", drawdown "
        << Format::percent(result.maxDrawdown) << std::endl;
    return true;
}

void
RiskTuning::printTable(std::ostream& out, const std::vector<Candidate>& rows, bool withTest) const
{
    std::vector<std::string> columns = RiskComparison::TABLE_COLUMNS;
    std::vector<bool> right = RiskComparison::TABLE_RIGHT_ALIGNED;
    columns.push_back("Within limit");
    right.push_back(false);
    if (withTest)
    {
        columns.insert(columns.begin() + 1, "Period");
        right.insert(right.begin() + 1, false);
    }

    TextTable table(columns, right);
    for (const auto& row : rows)
    {
        auto addPeriod = [&](const LiveTrader::Result& result, const std::string& period, const std::string& name) {
            std::vector<std::string> cells = RiskComparison::tableRow(name, result);
            cells.push_back(withinLimit(result) ? "yes" : "no");
            if (withTest)
                cells.insert(cells.begin() + 1, period);
            table.addRow(cells);
        };
        addPeriod(row.train, "training", row.name);
        if (withTest)
            addPeriod(row.test, "test", "");
    }
    out << table.render() << "Within limit: max drawdown <= " << Format::percent(maxDrawdown, 0) << "\n";
}

std::string
RiskTuning::oneLine(const json& risk)
{
    std::string text = "{\"" + NAME + "\": " + risk.value(NAME, json(FixedSizeRisk::NAME)).dump();
    for (const auto& [key, value] : risk.items())
        if (key != NAME)
            text += ", \"" + key + "\": " + value.dump();
    return text + "}";
}

bool
RiskTuning::saveToConfig(const std::string& configPath, const json& risk, const std::string& note)
{
    std::ifstream in(configPath);
    if (!in)
        return false;
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    in.close();

    // The top-level "risk" line, e.g.     "risk": {"name": "FIXED_SIZE", "quantity": 1},
    std::regex riskLine(R"re((\n(\s*)"risk": *\{[^\n]*\})(,?))re");
    std::smatch match;
    if (!std::regex_search(text, match, riskLine))
        return false;
    std::string indent = match[2];
    std::string comma = match[3];
    std::string replacement = "\n" + indent + "\"_risk_tuned\": " + json(note).dump() + ",\n" + indent + "\"risk\": "
                            + oneLine(risk) + comma;
    text.replace(match.position(0), match.length(0), replacement);

    // Only one note: drop an older one
    std::regex oldNote(R"re(\n\s*"_risk_tuned": *"[^\n]*",)re");
    std::string::const_iterator from = text.cbegin();
    std::smatch noteMatch;
    std::vector<std::pair<size_t, size_t>> notes;
    while (std::regex_search(from, text.cend(), noteMatch, oldNote))
    {
        size_t at = static_cast<size_t>(noteMatch.position(0) + (from - text.cbegin()));
        notes.push_back({at, static_cast<size_t>(noteMatch.length(0))});
        from = text.cbegin() + at + noteMatch.length(0);
    }
    if (notes.size() > 1)
        text.erase(notes.front().first, notes.front().second);

    // Check it's still valid JSON before writing
    if (json::parse(text, nullptr, false, true).is_discarded())
        return false;
    std::ofstream(configPath) << text;
    return true;
}
