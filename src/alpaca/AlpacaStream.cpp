#include "AlpacaStream.hpp"
#include "../util/Log.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "AlpacaApi.hpp"
#include "../util/MarketTime.hpp"

namespace
{
    // How far back to look for the bars strategies need: a few days covers a weekend or a holiday,
    // plus a day for every ~300 bars (IEX skips minutes with no trades, so a day has under 390)
    const int EXTRA_DAYS_TO_SEARCH = 4;
    const int IEX_BARS_PER_DAY = 300;

    // Alpaca's stream messages: "T" says what kind each one is
    const std::string BAR_MESSAGE = "b";
    const std::string SUCCESS_MESSAGE = "success";
    const std::string SUBSCRIPTION_MESSAGE = "subscription";
    const std::string ERROR_MESSAGE = "error";
    const std::string CONNECTED = "connected";
    const std::string AUTHENTICATED = "authenticated";

    const char* const UTC_TIME_FORMAT = UTC_TIME_FORMAT;
}

AlpacaStream::AlpacaStream(const json& config, const std::vector<std::string>& tickers,
                           std::shared_ptr<AlpacaClient> client)
    : client(client ? client : std::make_shared<AlpacaClient>())
{
    json alpaca = config.value("alpaca", json::object());

    feed = alpaca.value("data_feed", AlpacaApi::IEX_FEED);
    if (feed == AlpacaApi::TEST_FEED || tickers.empty())
        symbols = {AlpacaApi::TEST_SYMBOL};
    else
        symbols = tickers;
    symbol = symbols.front();

    streamUrl = alpaca.value("stream_url", AlpacaApi::STREAM_URL + feed);
    dataUrl = alpaca.value("data_url", AlpacaApi::DATA_URL);
}

std::vector<MarketCondition>
AlpacaStream::history(const std::string& ticker, int count)
{
    std::vector<MarketCondition> bars;

    if (feed == AlpacaApi::TEST_FEED || count <= 0)
        return bars; // The test stream has no history

    if (count > AlpacaApi::MAX_BARS_PER_REQUEST)
    {
        std::cerr << "WARNING - Strategies want " << count << " bars; loading the newest "
                  << AlpacaApi::MAX_BARS_PER_REQUEST << std::endl;
        count = AlpacaApi::MAX_BARS_PER_REQUEST;
    }

    int days = EXTRA_DAYS_TO_SEARCH + count / IEX_BARS_PER_DAY;
    std::time_t start = std::time(nullptr) - static_cast<std::time_t>(days) * MarketTime::SECONDS_PER_DAY;

    std::string url = AlpacaApi::barsUrl(dataUrl, ticker, AlpacaApi::ONE_MINUTE)
                    + "&feed=" + feed
                    + "&limit=" + std::to_string(count)
                    + "&sort=desc&start=" + MarketTime::formatUtc(start);

    HttpResult result = client->get(url);
    if (!result.ok() || !result.body.contains("bars") || !result.body["bars"].is_array())
    {
        std::cerr << "WARNING - Could not download recent " << ticker << " bars (" << result.status << "): "
                  << result.error << ". Strategies will start with no history." << std::endl;
        return bars;
    }

    for (const auto& bar : result.body["bars"])
        bars.push_back(AlpacaApi::parseMinuteBar(bar, ticker));

    std::reverse(bars.begin(), bars.end()); // We asked for newest first, strategies want oldest first
    Log::detail() << "Back-filled " << bars.size() << " of " << count << " recent " << ticker << " bars from Alpaca" << std::endl;
    return bars;
}

// e.g. "Alpaca's IEX stream, 1-minute bars"
std::string
AlpacaStream::describe() const
{
    std::string name = feed;
    std::transform(name.begin(), name.end(), name.begin(), ::toupper);
    return "Alpaca's " + name + " stream, 1-minute bars";
}

void
AlpacaStream::start(BarHandler onBar, std::function<void()>)
{
    barCallback = onBar;

    socket.setUrl(streamUrl);
    socket.setPingInterval(30);            // Keep the connection alive
    socket.enableAutomaticReconnection();  // Reconnect by itself if the connection drops
    socket.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) { onMessage(msg); });

    std::string names;
    for (const auto& s : symbols)
        names += (names.empty() ? "" : ", ") + s;
    Log::detail() << "Connecting to " << streamUrl << " for " << names << " 1-minute bars..." << std::endl;
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
            connected = true;
            Log::detail() << "Stream connected" << std::endl;
            break;

        case ix::WebSocketMessageType::Close:
            connected = false;
            std::cout << "Stream closed: " << msg->closeInfo.reason << std::endl;
            break;

        case ix::WebSocketMessageType::Error:
            connected = false;
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
                if (std::optional<json> reply = handleAlpacaMessage(message, barCallback))
                    socket.send(reply->dump());
            break;
        }

        default:
            break; // Ping/Pong/Fragment: nothing to do
    }
}

std::optional<json>
AlpacaStream::handleAlpacaMessage(const json& message, const BarHandler& onBar)
{
    std::string type = message.value("T", "");

    if (type == BAR_MESSAGE)
    {
        MarketCondition bar = AlpacaApi::parseMinuteBar(message, symbol);   // printed by the app with the P&L

        if (onBar)
            onBar(bar);
    }
    else if (type == SUCCESS_MESSAGE && message.value("msg", "") == CONNECTED)
    {
        // Step 1: log in
        return json{{"action", "auth"}, {"key", client->getKeyId()}, {"secret", client->getSecretKey()}};
    }
    else if (type == SUCCESS_MESSAGE && message.value("msg", "") == AUTHENTICATED)
    {
        // Step 2: ask for 1-minute bars for every ticker
        Log::detail() << "Stream authenticated, subscribing to " << symbols.size() << " ticker(s)" << std::endl;
        return json{{"action", "subscribe"}, {"bars", symbols}};
    }
    else if (type == SUBSCRIPTION_MESSAGE)
    {
        Log::detail() << "Subscribed: " << message.dump() << std::endl;
        std::cout << "Streaming " << message.value("bars", json::array()).size()
                  << " stock(s): waiting for the next minute's bars..." << std::endl;
    }
    else if (type == ERROR_MESSAGE)
    {
        // e.g. 402 auth failed, 406 connection limit exceeded (only one stream per account)
        std::cerr << "Alpaca stream error " << message.value("code", 0) << ": "
                  << message.value("msg", "") << std::endl;
    }
    return std::nullopt;
}
