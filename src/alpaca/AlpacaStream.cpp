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
    symbols = {symbol};

    std::string defaultStreamUrl = "wss://stream.data.alpaca.markets/v2/" + feed;
    streamUrl = alpaca.value("stream_url", defaultStreamUrl);
    dataUrl = alpaca.value("data_url", "https://data.alpaca.markets");
}

void
AlpacaStream::setSymbols(const std::vector<std::string>& tickers)
{
    if (feed == "test" || tickers.empty())
        return;
    symbols = tickers;
    symbol = tickers.front();
}

std::vector<MarketCondition>
AlpacaStream::fetchRecentBars(const std::string& ticker, int count)
{
    std::vector<MarketCondition> bars;

    if (feed == "test" || count <= 0)
        return bars; // The test stream has no history

    if (count > AlpacaClient::MAX_BARS_PER_REQUEST)
    {
        std::cerr << "WARNING - Strategies want " << count << " bars; loading the newest "
                  << AlpacaClient::MAX_BARS_PER_REQUEST << std::endl;
        count = AlpacaClient::MAX_BARS_PER_REQUEST;
    }

    int days = EXTRA_DAYS_TO_SEARCH + count / IEX_BARS_PER_DAY;
    std::time_t start = std::time(nullptr) - static_cast<std::time_t>(days) * MarketTime::SECONDS_PER_DAY;
    char startText[32];
    std::strftime(startText, sizeof(startText), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&start));

    std::string url = dataUrl + "/v2/stocks/" + ticker + "/bars"
                    + "?timeframe=1Min&feed=" + feed
                    + "&limit=" + std::to_string(count)
                    + "&sort=desc&start=" + startText;

    HttpResult result = client.get(url);
    if (!result.ok() || !result.body.contains("bars") || !result.body["bars"].is_array())
    {
        std::cerr << "WARNING - Could not download recent " << ticker << " bars (" << result.status << "): "
                  << result.error << ". Strategies will start with no history." << std::endl;
        return bars;
    }

    for (const auto& bar : result.body["bars"])
        bars.push_back(parseBar(bar, ticker));

    std::reverse(bars.begin(), bars.end()); // We asked for newest first, strategies want oldest first
    std::cout << "Back-filled " << bars.size() << " of " << count << " recent " << ticker << " bars from Alpaca" << std::endl;
    if (static_cast<int>(bars.size()) < count)
        std::cerr << "WARNING - Only " << bars.size() << " bars available; strategies needing more will "
                  << "start deciding once enough live bars arrive" << std::endl;
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

    std::string names;
    for (const auto& s : symbols)
        names += (names.empty() ? "" : ", ") + s;
    std::cout << "Connecting to " << streamUrl << " for " << names << " 1-minute bars..." << std::endl;
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
        MarketCondition bar = parseBar(message, symbol);   // printed by the app with the P&L

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
        // Step 2: ask for 1-minute bars for every ticker
        json subscribe = {{"action", "subscribe"}, {"bars", symbols}};
        socket.send(subscribe.dump());
        std::cout << "Stream authenticated, subscribing to " << symbols.size() << " ticker(s)" << std::endl;
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
