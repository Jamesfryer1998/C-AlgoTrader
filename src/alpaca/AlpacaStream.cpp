#include "AlpacaStream.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "../util/MarketTime.hpp"

AlpacaStream::AlpacaStream(const json& config)
{
    json alpaca = config.value("alpaca", json::object());

    feed = alpaca.value("data_feed", "iex");
    symbol = (feed == "test") ? "FAKEPACA" : config.at("ticker").get<std::string>();

    std::string defaultStreamUrl = "wss://stream.data.alpaca.markets/v2/" + feed;
    streamUrl = alpaca.value("stream_url", defaultStreamUrl);
    dataUrl = alpaca.value("data_url", "https://data.alpaca.markets");
}

std::vector<MarketCondition>
AlpacaStream::fetchRecentBars(int count)
{
    std::vector<MarketCondition> bars;

    if (feed == "test" || count <= 0)
        return bars; // The test stream has no history

    // Look back a few days so this still works on a Monday morning or after a holiday.
    std::time_t start = std::time(nullptr) - 4 * 24 * 60 * 60;
    char startText[32];
    std::strftime(startText, sizeof(startText), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&start));

    std::string url = dataUrl + "/v2/stocks/" + symbol + "/bars"
                    + "?timeframe=1Min&feed=" + feed
                    + "&limit=" + std::to_string(count)
                    + "&sort=desc&start=" + startText;

    HttpResult result = client.get(url);
    if (!result.ok() || !result.body.contains("bars") || !result.body["bars"].is_array())
    {
        std::cerr << "WARNING - Could not download recent bars (" << result.status << "): "
                  << result.error << ". Strategies will start with no history." << std::endl;
        return bars;
    }

    for (const auto& bar : result.body["bars"])
        bars.push_back(parseBar(bar, symbol));

    std::reverse(bars.begin(), bars.end()); // We asked for newest first, strategies want oldest first
    std::cout << "Loaded " << bars.size() << " recent " << symbol << " bars from Alpaca" << std::endl;
    return bars;
}

void
AlpacaStream::start(BarCallback onBar)
{
    barCallback = onBar;

    socket.setUrl(streamUrl);
    socket.setPingInterval(30);            // Keep the connection alive
    socket.enableAutomaticReconnection();  // Reconnect by itself if the connection drops
    socket.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) { onMessage(msg); });

    std::cout << "Connecting to " << streamUrl << " for " << symbol << " 1-minute bars..." << std::endl;
    socket.start();
}

void
AlpacaStream::stop()
{
    socket.stop();
}

void
AlpacaStream::onMessage(const ix::WebSocketMessagePtr& msg)
{
    switch (msg->type)
    {
        case ix::WebSocketMessageType::Open:
            std::cout << "Stream connected" << std::endl;
            break;

        case ix::WebSocketMessageType::Close:
            std::cout << "Stream closed: " << msg->closeInfo.reason << std::endl;
            break;

        case ix::WebSocketMessageType::Error:
            std::cerr << "Stream error: " << msg->errorInfo.reason
                      << " (retrying in " << msg->errorInfo.wait_time << " ms)" << std::endl;
            break;

        case ix::WebSocketMessageType::Message:
        {
            // Alpaca always sends a JSON array of messages
            json messages = json::parse(msg->str, nullptr, false);
            if (messages.is_discarded() || !messages.is_array())
            {
                std::cerr << "Stream: ignoring unexpected message " << msg->str << std::endl;
                break;
            }
            for (const auto& message : messages)
                handleAlpacaMessage(message);
            break;
        }

        default:
            break; // Ping/Pong/Fragment: nothing to do
    }
}

void
AlpacaStream::handleAlpacaMessage(const json& message)
{
    std::string type = message.value("T", "");

    if (type == "b") // A new 1-minute bar
    {
        MarketCondition bar = parseBar(message, symbol);
        std::cout << "\n[" << bar.DateTime << " New York] " << bar.Ticker
                  << " close " << std::fixed << std::setprecision(2) << bar.Close
                  << " vol " << bar.Volume
                  << " | arrived " << std::setprecision(2)
                  << secondsSinceBarClose(message.value("t", "")) << "s after the minute closed"
                  << std::endl;

        if (barCallback)
            barCallback(bar);
    }
    else if (type == "success" && message.value("msg", "") == "connected")
    {
        // Step 1: log in
        json auth = {{"action", "auth"}, {"key", client.getKeyId()}, {"secret", client.getSecretKey()}};
        socket.send(auth.dump());
    }
    else if (type == "success" && message.value("msg", "") == "authenticated")
    {
        // Step 2: ask for 1-minute bars for our symbol
        json subscribe = {{"action", "subscribe"}, {"bars", {symbol}}};
        socket.send(subscribe.dump());
        std::cout << "Stream authenticated, subscribing to " << symbol << " bars" << std::endl;
    }
    else if (type == "subscription")
    {
        std::cout << "Subscribed: " << message.dump() << std::endl;
    }
    else if (type == "error")
    {
        // e.g. 402 auth failed, 406 connection limit exceeded (only one stream per account)
        std::cerr << "Alpaca stream error " << message.value("code", 0) << ": "
                  << message.value("msg", "") << std::endl;
    }
}

MarketCondition
AlpacaStream::parseBar(const json& bar, const std::string& symbol)
{
    // Alpaca time "2026-10-28T14:31:00Z" (UTC) -> New York exchange time "2026-10-28 10:31:00",
    // the same clock as the CSV files the backtester uses
    std::time_t utc = MarketTime::parseUtc(bar.value("t", ""));
    std::string time = utc >= 0 ? MarketTime::toNewYorkTime(utc) : "";

    return MarketCondition(
        time,
        bar.value("S", symbol),
        bar.value("o", 0.0f),
        bar.value("c", 0.0f),
        bar.value("v", 0),
        "1m");
}

double
AlpacaStream::secondsSinceBarClose(const std::string& barStartUtc)
{
    std::time_t barStart = MarketTime::parseUtc(barStartUtc);
    if (barStart < 0)
        return 0.0;

    auto barClose = std::chrono::system_clock::from_time_t(barStart) + std::chrono::minutes(1);
    auto delay = std::chrono::system_clock::now() - barClose;
    return std::chrono::duration<double>(delay).count();
}
