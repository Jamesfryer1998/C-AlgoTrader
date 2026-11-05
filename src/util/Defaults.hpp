#pragma once

/**
 * Defaults
 *
 * Values every part of the app falls back on when config.json doesn't say, kept in one place so the
 * backtester, the simulated broker, the P&L book and the reports all agree.
 */
namespace Defaults
{
    constexpr double STARTING_CAPITAL = 100000.0;   // $ in a fresh paper account
    constexpr int BACKTEST_DAYS = 7;                // a backtest with no dates covers the last week
}
