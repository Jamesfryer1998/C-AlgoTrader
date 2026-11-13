#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "../oms/Order.hpp"

using json = nlohmann::json;

/**
 * DayEnd
 *
 * Nothing is held overnight: the strategies trade 1-minute bars, so a position still open at the close is
 * one no strategy chose to keep, and overnight news can gap the price straight through its stop loss.
 * Set in config/risk.json, the same rule for live trading and every backtest (live's, the screen's, the
 * optimiser's):
 *
 *   "end_of_day": {"flat_at_close": true, "no_entries_minutes_before_close": 15, "close_minutes_before_close": 5}
 *
 * With a 16:00 close: from 15:45 New York time no order may open or add to a position (exits still go), and
 * from 15:55 every position is closed. Without "end_of_day" (or flat_at_close false) positions carry over.
 *
 * Times are when a bar ends: the bar stamped 15:54 covers 15:54 to 15:55, so it is the one that closes.
 */
class DayEnd
{
    public:
        enum class Phase { Trading, NoNewEntries, Close };

        static constexpr int DEFAULT_NO_ENTRIES_MINUTES = 15;
        static constexpr int DEFAULT_CLOSE_MINUTES = 5;
        static inline const std::string STRATEGY = "end_of_day";   // the closing orders' strategy name

        explicit DayEnd(const json& config);

        bool isOn() const { return flatAtClose; }

        // Where `minute` (minutes since midnight, New York) falls before a close at `closeMinute`
        Phase phaseAt(int minute, int closeMinute) const;

        // For a bar ("YYYY-MM-DD HH:MM:SS", New York, the minute it starts) with the regular 16:00 close.
        // Bars outside regular hours, and daily bars, are always Trading.
        Phase phaseOfBar(const std::string& newYorkDateTime) const;
        Phase phaseOfBar(const std::string& newYorkDateTime, int closeMinute) const;

        // Would this order open a position, or add to the one held? (An exit only reduces or closes it.)
        static bool opensOrAdds(const Order& order, float held);

        // A market order closing all of `held`, labelled as the end of day's
        static Order closingOrder(const std::string& ticker, float held, float price);

    private:
        bool flatAtClose = false;
        int noEntriesMinutes = DEFAULT_NO_ENTRIES_MINUTES;
        int closeMinutes = DEFAULT_CLOSE_MINUTES;
};
