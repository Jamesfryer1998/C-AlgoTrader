#pragma once

/**
 * Defaults
 *
 * Values every part of the app falls back on when the config doesn't say, kept in one place so the
 * strategy tester, the backtest, the simulated broker, the P&L book and the reports all agree.
 */
namespace Defaults
{
    constexpr double STARTING_CAPITAL = 100000.0;   // $ in a fresh paper account
    constexpr int BACKTEST_DAYS = 7;                // a backtest with no dates covers the last week

    // Backtest costs until set otherwise
    constexpr double COMMISSION_PER_TRADE = 1.0;    // $
    constexpr double SLIPPAGE = 0.0005;             // 0.05% of the price

    // Where market data and records live, under the project root (config "marketDataBasePath")
    inline constexpr const char* DATA_FOLDER = "data/";
}
