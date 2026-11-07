#include "TopPicksReport.hpp"

#include <fstream>
#include "RiskReport.hpp"
#include "ScreenReport.hpp"
#include "../util/Format.hpp"
#include "../util/TextTable.hpp"
#include "../util/SaveData.hpp"

namespace
{
    // Fewer overlapping days than this and the picks' combined risk can't be measured
    const size_t MIN_DAYS_FOR_RISK = 60;

    // data/screen/<minute|daily>/
    const std::string PICKS_FILE = "/top_picks.csv";
    const std::string PICKS_CONFIG_FILE = "/top_picks_config.json";   // config.json strategy blocks, ready to paste
    const int JSON_INDENT = 4;
}

using Format::dollars;
using Format::number;
using Format::percent;
using Format::percentChange;

TopPicksReport::TopPicksReport(const std::vector<Screener::Row>& rows, const Screener& screener, size_t count,
                               double portfolioValue, const std::map<std::string, std::vector<MarketCondition>>& bars)
    : screener(screener), count(count), portfolioValue(portfolioValue), picks(Screener::topPicks(rows, count))
{
    if (picks.size() >= 2)
        pickValues = screener.dailyValues(picks, bars);
}

void
TopPicksReport::print(std::ostream& out) const
{
    printPicks(out);
    printRisk(out);
}

void
TopPicksReport::printPicks(std::ostream& out) const
{
    out << "\n=== Top " << count << " to take forward ===\n"
        << "Held up in both periods (Sharpe >= " << number(Screener::HELD_UP_SHARPE, 1)
        << " in training and test, beat buy and hold, " << Screener::PICK_TEST_TRADES << "+ test trades),\n"
        << "ranked by the weaker of the two Sharpe ratios so both periods count, one strategy per stock.\n";
    if (picks.empty())
    {
        out << "  Nothing passed. That is a real result: none of the tuned strategies did well enough on\n"
            << "  data they weren't tuned on to be worth trading over buying and holding.\n";
        return;
    }
    if (picks.size() < count)
        out << "  Only " << picks.size() << " passed.\n";
    out << "\n" << ScreenReport::combinationTable(picks, true)
        << "  * return on a full position (max_position_size shares at the first test price)\n";
}

void
TopPicksReport::printRisk(std::ostream& out) const
{
    if (picks.size() < 2)
        return;
    if (pickValues.days() < MIN_DAYS_FOR_RISK || pickValues.symbols.size() < 2)
    {
        out << "\n  (Not enough overlapping history to measure their combined risk.)\n";
        return;
    }

    quant::RiskSettings settings;
    settings.portfolioValue = portfolioValue;
    quant::RiskResult risk = quant::MonteCarloRisk::run(pickValues, settings);

    TextTable perPick({"Pick", "Return/yr", "Vol/yr"}, {false, true, true});
    std::vector<std::string> numbers;
    for (size_t i = 0; i < risk.symbols.size(); i++)
    {
        perPick.addRow({risk.symbols[i], percentChange(risk.annualReturn[i]), percent(risk.annualVolatility[i])});
        numbers.push_back("#" + std::to_string(i + 1));
    }

    auto loss = [&](const std::string& name, double fraction, const std::string& meaning) {
        return std::vector<std::string>{name, percent(fraction), dollars(fraction * portfolioValue, 0), meaning};
    };
    TextTable losses({"", "Loss", "In $", ""}, {false, true, true, false});
    losses.addRow(loss("VaR 95%", risk.at95.var, "1 month in 20 loses at least this"));
    losses.addRow(loss("Exp. shortfall", risk.at95.es, "average loss in those worst months"));
    losses.addRow(loss("VaR 99%", risk.at99.var, ""));
    losses.addRow(loss("ES 99%", risk.at99.es, ""));

    out << "\nRisk of trading these " << risk.symbols.size() << " together (Monte Carlo, " << settings.simulations
        << " simulated months, " << dollars(portfolioValue, 0) << " split equally)\n"
        << "From each pick's daily P&L with its chosen setting, " << pickValues.dates.front() << " to "
        << pickValues.dates.back() << ", as a return on a full position.\n\n"
        << perPick.render("  ")
        << "\n  Correlation of daily P&L\n" << RiskReport::correlationTable(numbers, risk.correlation)
        << "\n  Combined volatility " << percent(risk.portfolioAnnualVolatility) << "/yr (if uncorrelated: "
        << percent(risk.portfolioAnnualVolatilityUncorrelated) << ")\n"
        << "  Over a month: average " << percentChange(risk.meanReturn) << ", chance of a loss "
        << percent(risk.probabilityOfLoss, 0) << "\n\n"
        << losses.render("  ")
        << "\n  Real months that lost more than VaR 95%: " << percent(risk.historicalBreachRate95) << " (model: 5%). "
        << (risk.worstHistoricalStart.empty() ? std::string("No real month lost money.")
                                              : "Worst real month: " + percentChange(risk.worstHistorical) + " from "
                                                    + risk.worstHistoricalStart + ".")
        << "\n  The training period is in this history and the settings were fitted to it, so expect returns to be\n"
        << "  lower than shown; the volatility and correlations are the more trustworthy part.\n";
}

std::vector<std::string>
TopPicksReport::save(const std::string& folder) const
{
    std::string picksPath = folder + PICKS_FILE, configPath = folder + PICKS_CONFIG_FILE;
    Screener::writeTopPicksCsv(picksPath, picks);

    // The same picks as config/strategy.json portfolio blocks, with the ticker each one is for
    json ready = json::array();
    for (const auto& pick : picks)
        ready.push_back({{"ticker", pick.symbol}, {"strategy", screener.strategySettings(pick)}});
    SaveData::write(configPath, ready.dump(JSON_INDENT) + "\n");
    return {picksPath, configPath};
}
