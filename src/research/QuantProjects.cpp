#include "QuantProjects.hpp"

#include <filesystem>
#include <iostream>
#include <map>
#include "CrossoverReport.hpp"
#include "PairsReport.hpp"
#include "RiskReport.hpp"
#include "../alpaca/AlpacaDailyBars.hpp"

QuantProjects::QuantProjects(const json& algoConfig, const std::string& dataRoot)
    : algoConfig(algoConfig), dataRoot(dataRoot), outputFolder(dataRoot + "quant")
{
}

int
QuantProjects::run(const QuantOptions& options)
{
    AlpacaDailyBars source(algoConfig, dataRoot + "daily");
    std::map<std::string, std::vector<quant::DailyBar>> bars;
    if (!source.loadAll(options.symbols, options.start, options.end, options.offline, bars))
        return 1;
    quant::PriceTable prices = quant::alignCloses(options.symbols, bars, options.start, options.end);
    if (prices.days() < 2)
    {
        std::cerr << "ERROR - The symbols have no trading days in common in that range\n";
        return 1;
    }

    std::filesystem::create_directories(outputFolder);
    switch (options.project)
    {
        case QuantOptions::Project::Risk:      runRisk(options, prices); break;
        case QuantOptions::Project::Pairs:     runPairs(options, prices); break;
        case QuantOptions::Project::Scan:      runScan(prices); break;
        case QuantOptions::Project::Crossover: runCrossover(options, prices); break;
    }
    return 0;
}

void
QuantProjects::runRisk(const QuantOptions& options, const quant::PriceTable& prices) const
{
    quant::RiskResult result = quant::MonteCarloRisk::run(prices, options.risk);
    RiskReport report(prices, options.risk, result);
    report.print(std::cout);
    std::cout << "\nSaved " << report.save(outputFolder) << " (simulated returns, worst first)\n";
}

void
QuantProjects::runPairs(const QuantOptions& options, const quant::PriceTable& prices) const
{
    quant::PairsResult result = quant::PairsTrading::backtest(prices.symbols[0], prices.symbols[1], prices.dates,
                                                              prices[0], prices[1], options.pairs);
    PairsReport report(prices, options.pairs, result);
    report.print(std::cout);
    std::string saved = report.save(outputFolder);
    std::cout << "\nSaved " << saved << "_daily.csv and " << saved << "_trades.csv\n";
}

void
QuantProjects::runScan(const quant::PriceTable& prices) const
{
    std::vector<quant::PairScan> rows = quant::PairsTrading::scan(prices);
    PairScanReport report(prices, rows);
    report.print(std::cout);
    std::cout << "Saved " << report.save(outputFolder) << "\n";
}

void
QuantProjects::runCrossover(const QuantOptions& options, const quant::PriceTable& prices) const
{
    quant::CrossoverResult result = quant::MovingAverageCrossover::backtest(prices.dates, prices[0], options.crossover);
    CrossoverReport report(prices.symbols[0], options.crossover, result);
    report.print(std::cout);
    std::cout << "Saved " << report.save(outputFolder) << "\n";
}
