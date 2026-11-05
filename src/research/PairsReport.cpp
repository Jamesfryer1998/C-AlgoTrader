#include "PairsReport.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include "PerformanceTable.hpp"
#include "../util/Format.hpp"
#include "../util/TextTable.hpp"

using Format::number;
using Format::percent;
using Format::percentChange;

namespace
{
    // Half-life of the spread in days, or "never" if it doesn't revert
    std::string halfLife(double days)
    {
        return std::isfinite(days) ? number(days, 0) + "d" : "never";
    }

    std::string dateRange(const quant::PriceTable& prices)
    {
        return prices.dates.front() + " to " + prices.dates.back() + " (" + std::to_string(prices.days()) + " trading days)";
    }
}

PairsReport::PairsReport(const quant::PriceTable& prices, const quant::PairsSettings& settings,
                         const quant::PairsResult& result)
    : prices(prices), settings(settings), result(result)
{
}

void
PairsReport::print(std::ostream& out) const
{
    out << "\n=== Pairs trading: " << result.y << " vs " << result.x << " ===\n" << dateRange(prices) << "\n\n";
    printCointegration(out);
    printBacktest(out);
    printRecentTrades(out);
}

void
PairsReport::printCointegration(std::ostream& out) const
{
    const quant::CointegrationResult& whole = result.fullSample;
    out << "Whole period: log " << result.y << " = " << number(whole.alpha, 3) << " + " << number(whole.beta, 3)
        << " x log " << result.x << "\n"
        << "  Cointegrated? " << whole.verdict() << "   ADF " << number(whole.adf) << " (needs < "
        << quant::EG_CRITICAL_5 << " for 95%)   half-life " << halfLife(whole.halfLifeDays)
        << "   daily return correlation " << number(result.returnCorrelation) << "\n";

    TextTable years({"Year", "Hedge", "ADF", "Half-life", "Cointegrated?"}, {false, true, true, true, false});
    for (const auto& [year, test] : result.byYear)
        years.addRow({year, number(test.beta), number(test.adf), halfLife(test.halfLifeDays), test.verdict()});
    out << "\nYear by year (relationships break):\n" << years.render("  ");
}

void
PairsReport::printBacktest(std::ostream& out) const
{
    int reverted = 0, stopped = 0, wins = 0, daysHeld = 0;
    for (const auto& trade : result.trades)
    {
        if (trade.exitReason == quant::PairTrade::REVERTED) reverted++;
        if (trade.exitReason == quant::PairTrade::STOPPED) stopped++;
        if (trade.returnPct > 0) wins++;
        daysHeld += trade.days;
    }

    out << "\nBacktest: " << settings.lookbackDays << "-day rolling fit, enter |z| > " << number(settings.entryZ, 1)
        << ", exit |z| < " << number(settings.exitZ, 1)
        << (settings.stopZ > 0 ? ", stop |z| > " + number(settings.stopZ, 1) : std::string(", no stop"))
        << ", cost " << number(settings.costBps, 1) << " bps, signal delay " << settings.signalDelayDays << " day\n"
        << "  Trades " << result.trades.size() << " (" << reverted << " reverted, " << stopped << " stopped)";
    if (!result.trades.empty())
        out << ", won " << percent(static_cast<double>(wins) / result.trades.size(), 0) << ", average "
            << daysHeld / static_cast<int>(result.trades.size()) << " days held";
    out << ", in the market " << percent(result.timeInMarket, 0) << " of the time\n\n";

    PerformanceTable performance;
    performance.add("Pair strategy (after costs)", result.performance);
    out << performance.render();
}

void
PairsReport::printRecentTrades(std::ostream& out) const
{
    if (result.trades.empty())
        return;
    TextTable trades({"Entry", "Exit", "Side", "z in", "z out", "Days", "Return", "Why"},
                     {false, false, false, true, true, true, true, false});
    size_t from = result.trades.size() > RECENT_TRADES_SHOWN ? result.trades.size() - RECENT_TRADES_SHOWN : 0;
    for (size_t i = from; i < result.trades.size(); i++)
    {
        const quant::PairTrade& trade = result.trades[i];
        std::string side = trade.side > 0 ? "buy " + result.y + "/sell " + result.x : "sell " + result.y + "/buy " + result.x;
        trades.addRow({trade.entryDate, trade.exitDate, side, number(trade.entryZ, 1), number(trade.exitZ, 1),
                       std::to_string(trade.days), percentChange(trade.returnPct), trade.exitReason});
    }
    out << "\n  Recent trades\n" << trades.render("  ");
}

std::string
PairsReport::save(const std::string& folder) const
{
    std::string base = folder + "/pairs_" + result.y + "_" + result.x;

    std::ofstream daily(base + "_daily.csv");
    daily << "date,z,hedge_ratio,position,return\n";
    for (size_t i = 0; i < result.dates.size(); i++)
        daily << result.dates[i] << "," << result.z[i] << "," << result.hedgeRatio[i] << "," << result.position[i]
              << "," << result.dailyReturns[i] << "\n";

    std::ofstream trades(base + "_trades.csv");
    trades << "entry_date,exit_date,side,hedge_ratio,entry_z,exit_z,days,return,exit_reason\n";
    for (const auto& t : result.trades)
        trades << t.entryDate << "," << t.exitDate << "," << t.side << "," << t.hedgeRatio << "," << t.entryZ << ","
               << t.exitZ << "," << t.days << "," << t.returnPct << "," << t.exitReason << "\n";
    return base;
}

PairScanReport::PairScanReport(const quant::PriceTable& prices, const std::vector<quant::PairScan>& rows)
    : prices(prices), rows(rows)
{
}

void
PairScanReport::print(std::ostream& out) const
{
    TextTable table({"Y", "X", "ADF", "Cointegrated?", "Hedge", "Half-life", "Return corr"},
                    {false, false, true, false, true, true, true});
    size_t shown = std::min(rows.size(), ROWS_SHOWN);
    for (size_t i = 0; i < shown; i++)
    {
        const quant::PairScan& row = rows[i];
        table.addRow({row.y, row.x, number(row.test.adf), row.test.verdict(), number(row.test.beta),
                      halfLife(row.test.halfLifeDays), number(row.returnCorrelation)});
    }

    out << "\n=== Pair scan: Engle-Granger cointegration on log prices ===\n" << dateRange(prices) << "\n\n"
        << table.render("  ");
    if (shown < rows.size())
        out << "  ... " << rows.size() - shown << " more in the CSV\n";
    out << "\nTesting " << rows.size() << " pairs at 95% finds about " << number(rows.size() * FALSE_POSITIVE_RATE, 1)
        << " 'cointegrated' by chance alone. Check a candidate year by year with: quant_app pairs --pair Y,X\n";
}

std::string
PairScanReport::save(const std::string& folder) const
{
    std::string path = folder + "/pair_scan.csv";
    std::ofstream file(path);
    file << "y,x,adf,verdict,hedge_ratio,half_life_days,return_correlation\n";
    for (const auto& row : rows)
        file << row.y << "," << row.x << "," << row.test.adf << "," << row.test.verdict() << "," << row.test.beta << ","
             << row.test.halfLifeDays << "," << row.returnCorrelation << "\n";
    return path;
}
