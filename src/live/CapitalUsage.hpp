#pragma once

#include <map>
#include <string>

/**
 * CapitalUsage
 *
 * How much money was tied up in positions at once: after each stock's bar, the value of every position
 * (shares x price, shorts counted at their full size) added up, and the highest that total reached.
 *
 *   Most capital in use: $2,463.10 (2.46% of $100,000.00), at 2026-10-01 11:42 New York
 */
class CapitalUsage
{
    public:
        // A stock's position after its bar (quantity: + long, - short)
        void update(const std::string& ticker, double quantity, double price, const std::string& dateTime);

        double peak() const { return peakValue; }

        // Every position's value now, and one stock's
        double current() const;
        double valueOf(const std::string& ticker) const;
        const std::string& peakTime() const { return peakAt; }

        // The line above; startingCapital gives the share of the account
        std::string describe(double startingCapital) const;

    private:
        std::map<std::string, double> valueByTicker;
        double currentValue = 0;
        double peakValue = 0;
        std::string peakAt;   // "YYYY-MM-DD HH:MM:SS" New York
};
