#include "CrossoverReport.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include "PerformanceTable.hpp"
#include "../util/Format.hpp"
#include "../util/SaveData.hpp"

namespace
{
    // data/quant/macross_<SYMBOL>.csv
    const std::string FILE_PREFIX = "/macross_";
    const std::string FILE_EXTENSION = ".csv";
    const std::string CSV_HEADER = "date,close,fast_ma,slow_ma,position,strategy_return,buy_hold_return";
}

using Format::number;
using Format::percent;
using Format::percentChange;

CrossoverReport::CrossoverReport(const std::string& symbol, const quant::CrossoverSettings& settings,
                                 const quant::CrossoverResult& result)
    : symbol(symbol), settings(settings), result(result)
{
}

void
CrossoverReport::print(std::ostream& out) const
{
    out << "\n=== Moving average crossover: " << symbol << ", " << settings.fastDays << "/" << settings.slowDays
        << "-day ===\n" << result.dates.front() << " to " << result.dates.back() << " (" << result.dates.size()
        << " trading days, from the first day both averages exist)\n"
        << "Invested while the " << settings.fastDays << "-day average is above the " << settings.slowDays
        << "-day, otherwise cash. Cost " << number(settings.costBps, 1) << " bps per buy or sell.\n\n";

    PerformanceTable performance;
    performance.add("Crossover (" + std::to_string(settings.signalDelayDays) + "-day delay, costs)", result.strategy);
    performance.add("  same, without costs", result.strategyNoCosts);
    performance.add("  same, with look-ahead", result.lookAhead);
    performance.add("Buy and hold", result.buyHold);
    out << performance.render();

    out << "\n  " << result.trades << " buys, invested " << percent(result.timeInMarket, 0) << " of the time, costs "
        << percent(result.costsPaid, 2) << " of capital in total\n";

    double cagrGap = result.strategy.cagr - result.buyHold.cagr;
    out << "\nVerdict: the crossover " << (cagrGap >= 0 ? "beat" : "lagged") << " buy and hold by "
        << percent(std::fabs(cagrGap)) << "/yr, Sharpe " << number(result.strategy.sharpe) << " vs "
        << number(result.buyHold.sharpe) << ", worst drawdown " << percent(result.strategy.maxDrawdown, 0) << " vs "
        << percent(result.buyHold.maxDrawdown, 0) << ".\n"
        << "Look-ahead (deciding with the same close it trades on) changes CAGR by "
        << percentChange(result.lookAhead.cagr - result.strategy.cagr)
        << "/yr: any gain there is profit nobody could have had.\n";
}

std::string
CrossoverReport::save(const std::string& folder) const
{
    std::string path = folder + FILE_PREFIX + symbol + FILE_EXTENSION;
    std::ostringstream file;
    file << CSV_HEADER << "\n";
    for (size_t i = 0; i < result.dates.size(); i++)
        file << result.dates[i] << "," << result.closes[i] << "," << result.fastAverage[i] << "," << result.slowAverage[i]
             << "," << result.position[i] << "," << result.strategyReturns[i] << "," << result.buyHoldReturns[i] << "\n";
    SaveData::write(path, file.str());
    return path;
}
