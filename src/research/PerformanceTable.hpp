#pragma once

#include <string>
#include "../quant/Stats.hpp"
#include "../util/Format.hpp"
#include "../util/TextTable.hpp"

/**
 * PerformanceTable
 *
 * Strategies side by side: total return, CAGR, volatility, Sharpe and worst drawdown.
 *
 *                                Total   CAGR    Vol  Sharpe  MaxDD
 *   ---------------------------  -----  -----  -----  ------  -----
 *   Crossover (1-day delay)      +212%  +9.1%  12.0%    0.80  -21.3%
 */
class PerformanceTable
{
    public:
        void add(const std::string& name, const quant::Performance& p)
        {
            table.addRow({name, Format::percentChange(p.totalReturn, 0), Format::percentChange(p.cagr),
                          Format::percent(p.annualVolatility), Format::number(p.sharpe),
                          Format::percent(-p.maxDrawdown)});
        }

        std::string render() const { return table.render("  "); }

    private:
        TextTable table{{"", "Total", "CAGR", "Vol", "Sharpe", "MaxDD"}, {false, true, true, true, true, true}};
};
