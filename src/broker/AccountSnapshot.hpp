#pragma once

/**
 * AccountSnapshot
 *
 * A broker account's value at one moment, for display (Alpaca's account, or the simulated broker's).
 */
struct AccountSnapshot
{
    bool available = false;   // false if the broker couldn't be asked
    double equity = 0;        // cash + market value of everything held
    double lastEquity = 0;    // equity at the previous close (or the start of the replayed day)
    double cash = 0;

    double todayPnL() const { return equity - lastEquity; }
};
