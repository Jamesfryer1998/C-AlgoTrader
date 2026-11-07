#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include "../data_access/BarFeed.hpp"
#include "../data_access/MarketCondition.hpp"

/**
 * ReplayStream
 *
 * Plays saved 1-minute bars back through the live app as if they were arriving from Alpaca, so the whole
 * process (bars -> strategies -> orders -> fills -> table) can be watched without real money or market hours.
 *
 * Bars come from the CSVs Alpaca downloads were saved to: data/minute/<TICKER>/ (screen_app) or data/
 * (backtest_app, e.g. NVDA). Each replayed minute hands every stock's bar for that minute to the callback,
 * in portfolio order, then waits 1/speed seconds (speed = replayed minutes per real second; 0 = no waiting).
 */
class ReplayStream : public BarFeed
{
    public:
        // Saved days loaded before the first replayed day, enough for any strategy's back-fill
        static constexpr int HISTORY_DAYS = 5;

        // dataRoot: the project's data/ folder. date: first day to replay (YYYY-MM-DD; empty = the latest day
        // every ticker has saved). days: how many trading days to replay from there.
        ReplayStream(const std::string& dataRoot, const std::vector<std::string>& tickers, std::string date,
                     int days, double speed);
        ~ReplayStream() { stop(); }

        // Reads the bars. False (with a message) if a ticker has nothing for the replay days.
        bool load();

        // The `count` bars before the replay starts, for back-fill (oldest first; fewer if not saved)
        std::vector<MarketCondition> history(const std::string& ticker, int count) override;

        // Replay on a background thread; onFinished is called after the last minute (not after stop())
        void start(BarHandler onBar, std::function<void()> onFinished) override;
        void stop() override;

        const std::vector<std::string>& getDays() const { return days; }
        size_t minutes() const { return byMinute.size(); }

        // Where a ticker's saved minute bars are: data/minute/<TICKER>/ if it exists, else data/
        static std::string folderFor(const std::string& dataRoot, const std::string& ticker);

        // Every trading day saved for a ticker (from the file names), oldest first
        static std::vector<std::string> savedDays(const std::string& dataRoot, const std::string& ticker);

    private:
        std::string dataRoot;
        std::vector<std::string> tickers;
        std::string firstDay;
        int dayCount;
        double speed;

        std::vector<std::string> days;                                   // the days being replayed
        std::map<std::string, std::vector<MarketCondition>> before;      // ticker -> bars before them (history)
        std::map<std::string, std::vector<MarketCondition>> byMinute;    // "YYYY-MM-DD HH:MM:SS" -> bars, portfolio order

        std::thread worker;
        std::atomic<bool> running{false};
};
