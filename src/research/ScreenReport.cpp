#include "ScreenReport.hpp"

#include <algorithm>
#include <set>
#include "../util/Format.hpp"
#include "../util/TextTable.hpp"

using Format::number;
using Format::percentChange;

namespace
{
    // "12 (40%)"
    std::string countAndShare(int part, int whole)
    {
        return std::to_string(part) + (whole > 0 ? " (" + number(100.0 * part / whole, 0) + "%)" : "");
    }
}

ScreenReport::ScreenReport(const std::vector<Screener::Row>& rows, const std::vector<std::string>& strategies)
    : rows(rows), strategies(strategies)
{
    for (const auto& row : rows)
        if (row.found)
            tradable++;
}

void
ScreenReport::print(std::ostream& out) const
{
    std::set<std::string> stocks;
    std::string testStart, testEnd;
    for (const auto& row : rows)
    {
        stocks.insert(row.symbol);
        if (!row.testStart.empty() && (testStart.empty() || row.testStart < testStart))
            testStart = row.testStart;
        testEnd = std::max(testEnd, row.testEnd);
    }

    out << "\n=== Strategy screen: " << stocks.size() << " stocks x " << strategies.size() << " strategies ===\n"
        << "Each strategy tuned per stock on the bars before " << testStart << ", then judged on "
        << testStart << " to " << testEnd << " (never used for tuning).\n\n";
    printByStrategy(out);
    printBestOnTraining(out);
    printHeldUp(out);
}

void
ScreenReport::printByStrategy(std::ostream& out) const
{
    TextTable table({"Strategy", "Tradable", "Made money", "Beat B&H", "Train Sharpe", "Test Sharpe", "Train->test corr"},
                    {false, true, true, true, true, true, true});
    for (const auto& s : Screener::summarise(rows, strategies))
        table.addRow({s.strategy, std::to_string(s.found) + "/" + std::to_string(s.stocks),
                      countAndShare(s.positiveTest, s.found), countAndShare(s.beatBuyHold, s.found),
                      number(s.medianTrainSharpe), number(s.medianTestSharpe), number(s.trainTestCorrelation)});

    out << "By strategy (Sharpe ratios are medians; made money and beat B&H are on the test period)\n"
        << table.render("  ")
        << "  Tradable: some setting made at least min_closed_trades trades in training. Beat B&H: better Sharpe\n"
        << "  than buying and holding that stock over the test period. Correlation near 0 means a good training\n"
        << "  result said nothing about the test period.\n";
}

void
ScreenReport::printBestOnTraining(std::ostream& out) const
{
    std::vector<Screener::Row> best = Screener::topByTraining(rows, BEST_ON_TRAINING_SHOWN);
    if (best.empty() || tradable == 0)
        return;

    double allTestSharpe = 0, bestTestSharpe = 0;
    int bestBeatBuyHold = 0;
    for (const auto& row : rows)
        if (row.found)
            allTestSharpe += row.test.sharpeRatio;
    for (const auto& row : best)
    {
        bestTestSharpe += row.test.sharpeRatio;
        if (row.beatsBuyHold())
            bestBeatBuyHold++;
    }

    out << "\nThe " << best.size() << " best combinations on the training period (what you'd pick knowing only the past)\n"
        << combinationTable(best)
        << "  * return on a full position (max_position_size shares at the first test price)\n\n"
        << "  Their average test Sharpe: " << number(bestTestSharpe / best.size()) << " vs "
        << number(allTestSharpe / tradable) << " for all " << tradable << " tradable combinations; "
        << bestBeatBuyHold << " of " << best.size() << " beat buy and hold.\n"
        << "  If these don't beat the average, the best backtests were mostly luck.\n";
}

void
ScreenReport::printHeldUp(std::ostream& out) const
{
    std::vector<Screener::Row> held = Screener::heldUp(rows);
    out << "\nHeld up: training Sharpe >= " << number(Screener::HELD_UP_SHARPE, 1) << ", test Sharpe >= "
        << number(Screener::HELD_UP_SHARPE, 1) << " with " << Screener::HELD_UP_TEST_TRADES
        << "+ trades, and beat buy and hold (" << held.size() << " found)\n";
    if (held.size() > HELD_UP_SHOWN)
        held.resize(HELD_UP_SHOWN);
    if (!held.empty())
        out << combinationTable(held);
    out << "  These are candidates to paper trade, not proof: with " << tradable
        << " combinations tried, some will pass by luck, and this list was filtered using the test period.\n";
}

std::string
ScreenReport::combinationTable(const std::vector<Screener::Row>& rows, bool numbered)
{
    std::vector<std::string> headers = {"Stock", "Strategy", "Train Sharpe", "Test Sharpe", "B&H Sharpe", "Test return*",
                                        "B&H return", "Test trades", "Settings"};
    std::vector<bool> rightAligned = {false, false, true, true, true, true, true, true, false};
    if (numbered)
    {
        headers.insert(headers.begin(), "#");
        rightAligned.insert(rightAligned.begin(), true);
    }

    TextTable table(headers, rightAligned);
    for (size_t i = 0; i < rows.size(); i++)
    {
        const Screener::Row& r = rows[i];
        std::vector<std::string> cells = {r.symbol, r.strategy, number(r.train.sharpeRatio), number(r.test.sharpeRatio),
                                          number(r.buyHoldSharpe), percentChange(r.testReturnOnMaxPosition, 0),
                                          percentChange(r.buyHoldReturn, 0), std::to_string(r.test.closedTrades),
                                          Screener::describe(r.parameters)};
        if (numbered)
            cells.insert(cells.begin(), std::to_string(i + 1));
        table.addRow(cells);
    }
    return table.render("  ");
}
