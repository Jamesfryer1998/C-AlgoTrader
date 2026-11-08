#pragma once

#include <ostream>
#include <string>
#include <vector>
#include "../live/LiveOptions.hpp"

/**
 * RiskOptions
 *
 * risk_app's command line:
 *
 *   risk_app --backtest 2025-11-03 --days 120                      compare the engines (RiskComparison)
 *   risk_app --tune --backtest 2025-11-03 --days 120 --test-days 40 --save
 *                                                                tune their settings (RiskTuning)
 */
struct RiskOptions
{
    static constexpr int NOT_GIVEN = -1;

    LiveOptions period;                  // the backtested days (backtestDay, backtestDays)
    std::vector<std::string> engines;    // only these engines (empty: all)
    bool tune = false;
    int testDays = NOT_GIVEN;            // tuning: days after the training days to check the best settings on
    double maxDrawdownPercent = NOT_GIVEN;   // tuning: the most a setting may lose from a high (else config / default)
    bool save = false;                   // tuning: write the chosen settings into config/risk.json's "risk"

    // Throws std::invalid_argument with a readable message
    static RiskOptions parse(int argc, char* argv[]);
    static void printUsage(std::ostream& out);
};
