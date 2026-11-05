#include "RiskReport.hpp"

#include <fstream>
#include "../util/Format.hpp"
#include "../util/TextTable.hpp"

using Format::dollars;
using Format::number;
using Format::percent;
using Format::percentChange;

RiskReport::RiskReport(const quant::PriceTable& prices, const quant::RiskSettings& settings,
                       const quant::RiskResult& result)
    : prices(prices), settings(settings), result(result)
{
}

void
RiskReport::print(std::ostream& out) const
{
    out << "\n=== Monte Carlo portfolio risk ===\n"
        << prices.dates.front() << " to " << prices.dates.back() << " (" << prices.days() << " trading days)\n\n";
    printStocks(out);
    printLosses(out);
    printRealityCheck(out);
}

void
RiskReport::printStocks(std::ostream& out) const
{
    TextTable stocks({"Stock", "Weight", "Return/yr", "Vol/yr"}, {false, true, true, true});
    for (size_t i = 0; i < result.symbols.size(); i++)
        stocks.addRow({result.symbols[i], percent(result.weights[i], 0), percentChange(result.annualReturn[i]),
                       percent(result.annualVolatility[i])});
    out << stocks.render("  ")
        << "\n  Correlation of daily returns\n" << correlationTable(result.symbols, result.correlation)
        << "\n  Portfolio volatility " << percent(result.portfolioAnnualVolatility)
        << "/yr  (if the stocks were uncorrelated: " << percent(result.portfolioAnnualVolatilityUncorrelated) << ")\n";
}

void
RiskReport::printLosses(std::ostream& out) const
{
    double value = settings.portfolioValue;
    TextTable losses({"", "Loss", "In $", ""}, {false, true, true, false});
    losses.addRow({"VaR 95%", percent(result.at95.var), dollars(result.at95.var * value, 0), "1 in 20 periods loses at least this"});
    losses.addRow({"Expected shortfall", percent(result.at95.es), dollars(result.at95.es * value, 0), "average loss in those worst 5%"});
    losses.addRow({"VaR 99%", percent(result.at99.var), dollars(result.at99.var * value, 0), "1 in 100"});
    losses.addRow({"ES 99%", percent(result.at99.es), dollars(result.at99.es * value, 0), ""});

    out << "\n" << settings.simulations << " simulated " << settings.horizonDays << "-day futures for "
        << dollars(value, 0) << ":\n"
        << "  Average return     " << percentChange(result.meanReturn) << "\n"
        << "  Chance of a loss   " << percent(result.probabilityOfLoss, 0) << "\n\n"
        << losses.render("  ")
        << "\nCorrelation drives the risk: the same stocks with zero correlation would have VaR 95% "
        << percent(result.uncorrelated95.var) << " and ES " << percent(result.uncorrelated95.es) << " (vs "
        << percent(result.at95.var) << " and " << percent(result.at95.es) << ").\n";
}

void
RiskReport::printRealityCheck(std::ostream& out) const
{
    out << "Reality check over " << result.historicalWindows << " real " << settings.horizonDays
        << "-day windows: losses beyond VaR 95% happened " << percent(result.historicalBreachRate95)
        << " of the time (model: 5%), beyond VaR 99% " << percent(result.historicalBreachRate99)
        << " (model: 1%).\nWorst real window: " << percentChange(result.worstHistorical) << " starting "
        << result.worstHistoricalStart << ".\n";
}

std::string
RiskReport::save(const std::string& folder) const
{
    std::string path = folder + "/risk_simulations.csv";
    std::ofstream file(path);
    file << "simulation,return,pnl\n";
    for (size_t i = 0; i < result.simulatedReturns.size(); i++)
        file << i + 1 << "," << result.simulatedReturns[i] << ","
             << result.simulatedReturns[i] * settings.portfolioValue << "\n";
    return path;
}

std::string
RiskReport::correlationTable(const std::vector<std::string>& names, const quant::Matrix& correlation)
{
    std::vector<std::string> headers = {""};
    headers.insert(headers.end(), names.begin(), names.end());
    std::vector<bool> rightAligned(headers.size(), true);
    rightAligned[0] = false;
    TextTable table(headers, rightAligned);
    for (size_t i = 0; i < names.size(); i++)
    {
        std::vector<std::string> row = {names[i]};
        for (size_t j = 0; j < names.size(); j++)
            row.push_back(number(correlation[i][j]));
        table.addRow(row);
    }
    return table.render("  ");
}
