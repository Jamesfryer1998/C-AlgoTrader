#pragma once

#include <ctime>
#include <string>
#include <nlohmann/json.hpp>
#include "../data_access/MarketCondition.hpp"

using json = nlohmann::json;

/**
 * AlpacaApi
 *
 * Alpaca's addresses, data feeds and limits, and the reading of its bar messages, shared by the stream,
 * the broker and the history downloads. config/general.json's "alpaca" section can point the URLs elsewhere
 * (live trading, or a local test server).
 */
namespace AlpacaApi
{
    inline const std::string PAPER_TRADING_URL = "https://paper-api.alpaca.markets";   // fake money
    inline const std::string DATA_URL = "https://data.alpaca.markets";
    inline const std::string STREAM_URL = "wss://stream.data.alpaca.markets/v2/";     // + the feed

    inline const std::string IEX_FEED = "iex";     // one exchange, free in real time
    inline const std::string SIP_FEED = "sip";     // all US exchanges, free once 15 minutes old
    inline const std::string TEST_FEED = "test";   // a fake stream for checking the connection
    inline const std::string TEST_SYMBOL = "FAKEPACA";   // the test stream's only symbol (can't be traded)

    inline const std::string ACCOUNT_PATH = "/v2/account";
    inline const std::string ORDERS_PATH = "/v2/orders";
    inline const std::string POSITIONS_PATH = "/v2/positions";
    inline const std::string CLOCK_PATH = "/v2/clock";   // is the market open, and its next open / close
    inline const std::string STOCKS_PATH = "/v2/stocks/";   // + symbol + what about it, e.g. "/bars"

    inline const std::string FIRST_HISTORY_DAY = "2016-01-01";   // Alpaca's bar history starts in 2016

    inline const std::string ONE_MINUTE = "1Min";
    inline const std::string ONE_DAY = "1Day";

    // The most bars Alpaca returns for one request (more come in pages)
    constexpr int MAX_BARS_PER_REQUEST = 10000;

    // The free plan can't query the most recent 15 minutes of SIP data: keep a minute clear of it
    constexpr std::time_t SIP_DELAY_SECONDS = 16 * 60;

    // Fields of a position, e.g. {"qty":"5","side":"long","avg_entry_price":"181.20","unrealized_pl":"1.5",...}
    inline const std::string QUANTITY = "qty";
    inline const std::string SIDE = "side";
    inline const std::string SHORT = "short";
    inline const std::string AVERAGE_PRICE = "avg_entry_price";
    inline const std::string UNREALISED_PNL = "unrealized_pl";
    inline const std::string UNREALISED_FRACTION = "unrealized_plpc";

    // Alpaca sends numbers as text, e.g. "100123.45": the number, or 0 if missing or unreadable
    double numberField(const json& body, const std::string& key);

    // Bars for one stock: <dataUrl>/v2/stocks/<SYMBOL>/bars?timeframe=<timeframe>
    // (add the other query parameters with "&...")
    std::string barsUrl(const std::string& dataUrl, const std::string& symbol, const std::string& timeframe);

    // The page token to ask for the next page of a bars response ("" when it was the last page)
    std::string nextPageToken(const json& body);

    // A 1-minute bar message, {"t":"2026-10-28T14:31:00Z","S":"NVDA","o":..,"c":..,"v":..}, as a bar in
    // New York exchange time (the CSV files' clock). symbol is used if the message has none.
    MarketCondition parseMinuteBar(const json& bar, const std::string& symbol);
}
