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
 *
 * Log::detail() is for the step-by-step notes of starting up (files loaded, connections made, bars
 * back-filled), which the live app sums up in one table instead. Shown when Log::details is on (--verbose).
 */
namespace Log
{
    inline bool verbose = true;
    inline bool details = false;

    // A stream that ignores everything
    inline std::ostream& silent()
    {
        thread_local std::ostream nowhere(nullptr);
        return nowhere;
    }

    inline std::ostream& out()
    {
        return verbose ? std::cout : silent();
    }

    inline std::ostream& detail()
    {
        return details ? std::cout : silent();
    }
}
