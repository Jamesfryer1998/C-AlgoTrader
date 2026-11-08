#pragma once

#include <functional>
#include <string>
#include <vector>
#include "MarketCondition.hpp"

/**
 * BarFeed
 *
 * Where the live app's 1-minute bars come from: Alpaca's live stream (AlpacaStream) or saved bars played back
 * (BacktestStream). The app treats both the same way:
 *
 *   feed.history(ticker, count)        the bars before now, so strategies can decide from the first new one
 *   feed.start(onBar, onFinished)      new bars arrive on onBar (on the feed's own thread)
 *   feed.stop()
 */
class BarFeed
{
    public:
        using BarHandler = std::function<void(const MarketCondition&)>;

        virtual ~BarFeed() = default;

        // The `count` bars before now for one stock, oldest first (fewer if there aren't that many)
        virtual std::vector<MarketCondition> history(const std::string& ticker, int count) = 0;

        // Start sending bars. Returns straight away. onFinished runs if the feed ends by itself (a backtest
        // reaching its last minute; a live stream never does).
        virtual void start(BarHandler onBar, std::function<void()> onFinished) = 0;
        virtual void stop() = 0;

        // Is the connection up? (for the dashboard's health panel; saved bars always are)
        virtual bool isConnected() const { return true; }

        // Where the bars come from, for the start-up summary, e.g. "Alpaca's IEX stream, 1-minute bars"
        virtual std::string describe() const = 0;
};
