#pragma once

#include <functional>
#include <string>
#include <vector>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>
#include "AlpacaClient.hpp"
#include "../data_access/MarketCondition.hpp"

using json = nlohmann::json;

/**
 * AlpacaStream
 *
 * Real-time market data from Alpaca over a WebSocket.
 * Alpaca pushes a 1-minute bar (open/high/low/close/volume) for our ticker as soon as each
 * minute closes. Every bar is converted to a MarketCondition and handed to the callback.
 *
 * Config (src/config/config.json -> "alpaca"):
 *   "data_feed": "iex"  -> free, real-time, IEX exchange only
 *                "sip"  -> all US exchanges, needs Alpaca's paid Algo Trader Plus plan
 *                "test" -> Alpaca's test stream: fake symbol FAKEPACA, runs 24/7 (good for checking the wiring)
 *
 * The callback runs on the WebSocket's background thread, one bar at a time.
 * If the connection drops, IXWebSocket reconnects automatically and we log in again.
 */
class AlpacaStream
{
    public:
        using BarCallback = std::function<void(const MarketCondition&)>;

        AlpacaStream(const json& config);

        // Download the most recent `count` 1-minute bars over REST so strategies have history straight away
        // (oldest first). The live app asks for exactly what the active strategies need.
        std::vector<MarketCondition> fetchRecentBars(int count);

        // Connect and start streaming. Returns immediately; bars arrive on onBar.
        void start(BarCallback onBar);
        void stop();

        std::string getSymbol() const { return symbol; }
        std::string getFeed() const { return feed; }

        // Convert one Alpaca bar JSON object, e.g. {"S":"NVDA","o":1,"c":2,"v":3,"t":"2026-10-28T14:31:00Z"}
        static MarketCondition parseBar(const json& bar, const std::string& symbol);

    private:
        void onMessage(const ix::WebSocketMessagePtr& msg);
        void handleAlpacaMessage(const json& message);
        static double secondsSinceBarClose(const std::string& barStartUtc);

        std::string feed;
        std::string symbol;
        std::string streamUrl;
        std::string dataUrl;

        AlpacaClient client;
        ix::WebSocket socket;
        BarCallback barCallback;
};
