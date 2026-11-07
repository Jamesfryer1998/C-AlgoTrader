#include "RiskComparison.hpp"

#include <algorithm>
#include "../risk/RiskEngineFactory.hpp"
#include "../util/CommandLine.hpp"
#include "../util/Format.hpp"
#include "../util/TextTable.hpp"

namespace
{
    const std::string COMPARISON = "risk_comparison";
    const std::string RISK = "risk";
    const std::string NAME = "name";
}

RiskComparison::RiskComparison(const json& config, const std::string& dataRoot, const LiveOptions& replay)
    : config(config), dataRoot(dataRoot), replay(replay)
{
    this->replay.replay = true;
    this->replay.quiet = true;
    this->replay.replaySpeed = 0;   // as fast as possible
}

LiveOptions
RiskComparison::parse(int argc, char* argv[])
{
    LiveOptions options;
    options.replay = true;
    CommandLine args(argc, argv);
    while (args.next())
    {
        if (args.is("--help"))
            options.showHelp = true;
        else if (args.is("--replay"))
            options.replayDay = args.text();
        else if (args.is("--days"))
            options.replayDays = std::max(1, args.integer());
        else if (args.is("--engines"))
            options.riskEngines = args.list();
        else
            args.rejectOption();
    }
    if (!options.showHelp && options.replayDay.empty())
        throw std::invalid_argument("Say which days to replay, e.g. --replay 2025-11-03 --days 250");
    return options;
}

void
RiskComparison::printUsage(std::ostream& out)
{
    out << R"(Compare risk engines (position sizing) on the same replayed days

Usage: risk_app --replay YYYY-MM-DD [--days N] [--engines RISK_PER_TRADE,FIXED_FRACTION]
  --replay 2025-11-03       first day to replay (bars not saved are downloaded, as for algo_trader_app)
  --days 250                trading days to replay (default 1)
  --engines A,B             only these engines (default: all, or config.json's "risk_comparison")

Engines: FIXED_SIZE, FIXED_FRACTION, RISK_PER_TRADE, VOLATILITY_TARGET. Settings for each can be listed in
config.json, e.g. "risk_comparison": [{"name": "RISK_PER_TRADE", "risk_percent": 0.5}, ...]
)";
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
    std::vector<json> blocks = engines(config, replay.riskEngines);
    if (blocks.empty())
    {
        out << "No risk engines to compare (check --engines and \"risk_comparison\")" << std::endl;
        return 1;
    }

    for (const auto& block : blocks)
    {
        json withEngine = config;
        withEngine[RISK] = block;
        std::string label = RiskEngineFactory::create(withEngine)->describe();
        out << "Replaying with " << label << "..." << std::flush;

        LiveTrader trader(withEngine, replay, dataRoot);
        if (trader.run() != 0)
        {
            out << " failed" << std::endl;
            return 1;
        }
        const LiveTrader::Result& result = trader.getResult();
        out << " " << Format::percentChange(result.endingEquity / result.startingEquity - 1.0) << std::endl;
        labels.push_back(label);
        results.push_back(result);
    }
    printTable(out);
    return 0;
}

void
RiskComparison::printTable(std::ostream& out) const
{
    TextTable table({"Risk engine", "Return", "Profit", "Max drawdown", "Sharpe", "Most capital in use", "Fills"},
                    {false, true, true, true, true, true, true});
    for (size_t i = 0; i < results.size(); i++)
    {
        const LiveTrader::Result& r = results[i];
        double share = r.startingEquity > 0 ? r.mostCapitalInUse / r.startingEquity : 0;
        table.addRow({labels[i], Format::percentChange(r.endingEquity / r.startingEquity - 1.0),
                      Format::dollarChange(r.endingEquity - r.startingEquity, 0), Format::percent(r.maxDrawdown),
                      Format::number(r.sharpe), Format::dollars(r.mostCapitalInUse, 0) + " (" + Format::percent(share, 0) + ")",
                      std::to_string(r.fills)});
    }
    out << "\n=== Risk engines on the same " << (results.empty() ? 0 : results.front().days) << " replayed day(s) ===\n"
        << table.render()
        << "Same portfolio, strategies and simulated broker (slippage on every fill); only the sizing changes.\n"
        << "Sharpe is from daily returns. Settings were tuned at 1 share: bigger sizes mean more slippage live.\n";
}
