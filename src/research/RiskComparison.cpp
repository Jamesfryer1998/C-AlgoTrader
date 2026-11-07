#include "RiskComparison.hpp"

#include <algorithm>
#include "../risk/RiskEngineFactory.hpp"
#include "../util/Format.hpp"
#include "../util/TextTable.hpp"

namespace
{
    const std::string COMPARISON = "risk_comparison";
    const std::string RISK = "risk";
    const std::string NAME = "name";
}

const std::vector<std::string> RiskComparison::TABLE_COLUMNS = {"Risk engine", "Return", "Profit", "Max drawdown",
                                                                "Sharpe", "Most capital in use", "Fills"};
const std::vector<bool> RiskComparison::TABLE_RIGHT_ALIGNED = {false, true, true, true, true, true, true};

RiskComparison::RiskComparison(const json& config, const std::string& dataRoot, const RiskOptions& options)
    : config(config), dataRoot(dataRoot), options(options)
{
}

LiveTrader::Result
RiskComparison::replayWith(const json& config, const std::string& dataRoot, const json& risk,
                           const std::string& firstDay, int days, bool& ok)
{
    LiveOptions replay;
    replay.replay = true;
    replay.quiet = true;
    replay.replaySpeed = 0;   // as fast as possible
    replay.replayDay = firstDay;
    replay.replayDays = days;

    json withEngine = config;
    withEngine[RISK] = risk;
    LiveTrader trader(withEngine, replay, dataRoot);
    ok = trader.run() == 0;
    return trader.getResult();
}

std::string
RiskComparison::label(const json& config, const json& risk)
{
    json withEngine = config;
    withEngine[RISK] = risk;
    return RiskEngineFactory::create(withEngine)->describe();
}

double
RiskComparison::returnOf(const LiveTrader::Result& result)
{
    return result.startingEquity > 0 ? result.endingEquity / result.startingEquity - 1.0 : 0;
}

std::vector<std::string>
RiskComparison::tableRow(const std::string& name, const LiveTrader::Result& r)
{
    double share = r.startingEquity > 0 ? r.mostCapitalInUse / r.startingEquity : 0;
    return {name, Format::percentChange(returnOf(r)), Format::dollarChange(r.endingEquity - r.startingEquity, 0),
            Format::percent(r.maxDrawdown), Format::number(r.sharpe),
            Format::dollars(r.mostCapitalInUse, 0) + " (" + Format::percent(share, 0) + ")", std::to_string(r.fills)};
}

std::vector<json>
RiskComparison::engines(const json& config, const std::vector<std::string>& names)
{
    std::vector<json> blocks;
    if (config.contains(COMPARISON) && config[COMPARISON].is_array() && !config[COMPARISON].empty())
        for (const auto& block : config[COMPARISON])
            blocks.push_back(block);
    else
        for (const auto& name : RiskEngineFactory::names())
            blocks.push_back({{NAME, name}});

    if (names.empty())
        return blocks;
    std::vector<json> chosen;
    for (const auto& block : blocks)
        if (std::find(names.begin(), names.end(), block.value(NAME, "")) != names.end())
            chosen.push_back(block);
    return chosen;
}

int
RiskComparison::run(std::ostream& out)
{
    std::vector<json> blocks = engines(config, options.engines);
    if (blocks.empty())
    {
        out << "No risk engines to compare (check --engines and \"risk_comparison\")" << std::endl;
        return 1;
    }

    for (const auto& block : blocks)
    {
        std::string name = label(config, block);
        out << "Replaying with " << name << "..." << std::flush;
        bool ok = false;
        LiveTrader::Result result = replayWith(config, dataRoot, block, options.replay.replayDay,
                                               options.replay.replayDays, ok);
        if (!ok)
        {
            out << " failed" << std::endl;
            return 1;
        }
        out << " " << Format::percentChange(returnOf(result)) << std::endl;
        labels.push_back(name);
        results.push_back(result);
    }
    printTable(out);
    return 0;
}

void
RiskComparison::printTable(std::ostream& out) const
{
    TextTable table(TABLE_COLUMNS, TABLE_RIGHT_ALIGNED);
    for (size_t i = 0; i < results.size(); i++)
        table.addRow(tableRow(labels[i], results[i]));
    out << "\n=== Risk engines on the same " << (results.empty() ? 0 : results.front().days) << " replayed day(s) ===\n"
        << table.render()
        << "Same portfolio, strategies and simulated broker (slippage on every fill); only the sizing changes.\n"
        << "Sharpe is from daily returns. Settings were tuned at 1 share: bigger sizes mean more slippage live.\n";
}
