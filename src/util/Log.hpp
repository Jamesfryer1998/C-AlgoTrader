#pragma once

#include <iostream>

/**
 * Log
 *
 * Per-bar chatter (RSI values, simulated fills, backtest steps) goes through Log::out() instead of
 * std::cout, so it can be switched off when running thousands of backtests:
 *
 *   Log::verbose = false;   // e.g. in the optimiser
 *
 * When off, Log::out() is a stream that ignores everything without even formatting it.
 * Errors still go to std::cerr.
 */
namespace Log
{
    inline bool verbose = true;

    inline std::ostream& out()
    {
        thread_local std::ostream silent(nullptr);
        return verbose ? std::cout : silent;
    }
}
