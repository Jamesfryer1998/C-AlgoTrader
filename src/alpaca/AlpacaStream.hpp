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
 * Alpaca pushes a 1-minute bar (open/high/low/close/volume) for each subscribed ticker as soon as each
 * minute closes. Every bar is converted to a MarketCondition (its Ticker says which stock) and handed to
 * the callback. One connection carries any number of tickers (Alpaca allows one stream per account).
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

        // Trade several tickers: subscribe to all of these instead of config.json's "ticker".
        // Ignored on the "test" feed, which only has FAKEPACA. Call before start().
        void setSymbols(const std::vector<std::string>& tickers);
        const std::vector<std::string>& getSymbols() const { return symbols; }

        // Download the most recent `count` 1-minute bars of one ticker over REST so strategies have history
        // straight away (oldest first). The live app asks for exactly what the active strategies need.
        std::vector<MarketCondition> fetchRecentBars(const std::string& ticker, int count);
        std::vector<MarketCondition> fetchRecentBars(int count) { return fetchRecentBars(symbol, count); }

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

        std::string feed;
        std::string symbol;                 // the first (or only) ticker
        std::vector<std::string> symbols;   // every subscribed ticker
        std::string streamUrl;
        std::string dataUrl;

        AlpacaClient client;
        ix::WebSocket socket;
        BarCallback barCallback;
};
