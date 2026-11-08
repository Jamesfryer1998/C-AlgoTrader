#pragma once

#include <functional>
#include <string>
#include <vector>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <optional>
#include "AlpacaClient.hpp"
#include "../data_access/BarFeed.hpp"
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
 * Config (config/general.json -> "alpaca"):
 *   "data_feed": "iex"  -> free, real-time, IEX exchange only
 *                "sip"  -> all US exchanges, needs Alpaca's paid Algo Trader Plus plan
 *                "test" -> Alpaca's test stream: fake symbol FAKEPACA, runs 24/7 (good for checking the wiring)
 *
 * The callback runs on the WebSocket's background thread, one bar at a time.
 * If the connection drops, IXWebSocket reconnects automatically and we log in again.
 */
class AlpacaStream : public BarFeed
{
    public:
        // Streams every one of `tickers` (the portfolio's stocks). The "test" feed only has FAKEPACA, so it
        // streams that instead.
        // client: for the REST back-fill and the stream's keys (none: a real AlpacaClient; tests pass a fake)
        AlpacaStream(const json& config, const std::vector<std::string>& tickers,
                     std::shared_ptr<AlpacaClient> client = nullptr);

        const std::vector<std::string>& getSymbols() const { return symbols; }

        // Download the most recent `count` 1-minute bars of one ticker over REST so strategies have history
        // straight away (oldest first). The live app asks for exactly what the active strategies need.
        std::vector<MarketCondition> history(const std::string& ticker, int count) override;

        // Connect and start streaming. Returns immediately; bars arrive on onBar. A live stream never finishes.
        void start(BarHandler onBar, std::function<void()> onFinished = nullptr) override;
        void stop() override;

        std::string getSymbol() const { return symbol; }
        std::string getFeed() const { return feed; }
        std::string describe() const override;
        bool isConnected() const override { return connected; }

        // One message from Alpaca's stream: a bar goes to onBar; the connected / authenticated steps return
        // what to send back (log in, then subscribe). Public so it can be tested without a connection.
        std::optional<json> handleAlpacaMessage(const json& message, const BarHandler& onBar);

    private:
        void onMessage(const ix::WebSocketMessagePtr& msg);

        std::string feed;
        std::string symbol;                 // the first (or only) ticker
        std::vector<std::string> symbols;   // every subscribed ticker
        std::string streamUrl;
        std::string dataUrl;

        std::shared_ptr<AlpacaClient> client;
        ix::WebSocket socket;
        std::atomic<bool> connected{false};
        BarHandler barCallback;
};
