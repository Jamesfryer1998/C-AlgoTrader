#pragma once

#include <string>
#include <vector>
#include "../data_access/MarketCondition.hpp"

/**
 * Loads the 1-minute bars AlpacaHistory saved (one CSV per trading day, e.g. marketData_NVDA_2026-10-28.csv)
 * for a date range, oldest first. Files are read in parallel.
 *
 * Only rows from each file's own day are kept (old multi-day Yahoo files shared the naming scheme), and
 * duplicate timestamps are dropped.
 */
namespace MinuteBars
{
    std::vector<MarketCondition> load(const std::string& dataDir, const std::string& ticker,
                                      const std::string& startDate, const std::string& endDate, int threads);
}
