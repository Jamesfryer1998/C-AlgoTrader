#include "AlpacaMultiBars.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <set>
#include "AlpacaApi.hpp"
#include "../util/MarketTime.hpp"

namespace
{
    const std::string MULTI_BARS_PATH = "/v2/stocks/bars";
    const std::string ASSETS_PATH = "/v2/assets?status=active&asset_class=us_equity";
    const std::string DEFAULT_ADJUSTMENT = "split";

    // Retry when rate limited or the connection drops: waits 5 s, 10 s, ... between six attempts
    const int MAX_ATTEMPTS = 6;
    const std::chrono::seconds FIRST_RETRY_WAIT{5};

    // The exchanges a listed stock or ETF trades on (Alpaca also lists OTC and crypto)
    const std::set<std::string> LISTED_EXCHANGES = {"NYSE", "NASDAQ", "ARCA", "AMEX", "BATS", "NYSEARCA"};
    const size_t LONGEST_SYMBOL = 5;
}

AlpacaMultiBars::AlpacaMultiBars(const json& config, std::shared_ptr<AlpacaClient> client)
    : client(client ? client : std::make_shared<AlpacaClient>())
{
    json alpaca = config.value("alpaca", json::object());
    dataUrl = alpaca.value("data_url", AlpacaApi::DATA_URL);
    tradingUrl = alpaca.value("trading_url", AlpacaApi::PAPER_TRADING_URL);
    feed = alpaca.value("history_feed", AlpacaApi::SIP_FEED);
    adjustment = alpaca.value("history_adjustment", DEFAULT_ADJUSTMENT);
}

IntradayBar
AlpacaMultiBars::parseBar(const json& bar)
{
    IntradayBar parsed;
    std::time_t utc = MarketTime::parseUtc(bar.value("t", ""));
    if (utc < 0)
        return parsed;
    parsed.time = MarketTime::toNewYorkTime(utc);
    parsed.open = bar.value("o", 0.0);
    parsed.high = bar.value("h", 0.0);
    parsed.low = bar.value("l", 0.0);
    parsed.close = bar.value("c", 0.0);
    parsed.volume = bar.value("v", 0.0);
    return parsed;
}

bool
AlpacaMultiBars::fetchBatch(const std::vector<std::string>& symbols, const std::string& timeframe,
                            const std::string& startUtc, const std::string& endUtc, BarsBySymbol& out)
{
    std::string joined;
    for (const auto& symbol : symbols)
        joined += (joined.empty() ? "" : ",") + symbol;

    std::string pageToken;
    do
    {
        std::string url = dataUrl + MULTI_BARS_PATH + "?symbols=" + joined + "&timeframe=" + timeframe
                        + "&start=" + startUtc + "&end=" + endUtc + "&limit="
                        + std::to_string(AlpacaApi::MAX_BARS_PER_REQUEST) + "&adjustment=" + adjustment
                        + "&feed=" + feed + "&sort=asc";
        if (!pageToken.empty())
            url += "&page_token=" + pageToken;

        HttpResult result = client->getWhenFree(url, MAX_ATTEMPTS, FIRST_RETRY_WAIT);
        if (!result.ok() || !result.body.is_object())
        {
            std::cerr << "ERROR - Alpaca bars request failed (" << result.status << "): " << result.error << std::endl;
            return false;
        }
        const json& bars = result.body.value("bars", json());
        if (bars.is_object())
            for (const auto& [symbol, list] : bars.items())
                for (const auto& bar : list)
                {
                    IntradayBar parsed = parseBar(bar);
                    if (!parsed.time.empty())
                        out[symbol].push_back(parsed);
                }
        pageToken = AlpacaApi::nextPageToken(result.body);
    } while (!pageToken.empty());
    return true;
}

AlpacaMultiBars::BarsBySymbol
AlpacaMultiBars::fetch(const std::vector<std::string>& symbols, const std::string& timeframe,
                       const std::string& startUtc, const std::string& endUtc, size_t symbolsPerRequest,
                       bool& ok)
{
    BarsBySymbol bars;
    ok = true;
    size_t perRequest = std::max<size_t>(1, symbolsPerRequest);
    for (size_t first = 0; first < symbols.size(); first += perRequest)
    {
        std::vector<std::string> batch(symbols.begin() + first,
                                       symbols.begin() + std::min(symbols.size(), first + perRequest));
        if (!fetchBatch(batch, timeframe, startUtc, endUtc, bars))
            ok = false;
    }
    return bars;
}

bool
AlpacaMultiBars::isListedStock(const json& asset)
{
    if (!asset.is_object() || !asset.value("tradable", false) || asset.value("status", "") != "active")
        return false;
    if (!LISTED_EXCHANGES.count(asset.value("exchange", "")))
        return false;
    std::string symbol = asset.value("symbol", "");
    return !symbol.empty() && symbol.size() <= LONGEST_SYMBOL
        && std::all_of(symbol.begin(), symbol.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
}

std::vector<std::string>
AlpacaMultiBars::tradableSymbols(bool& ok)
{
    std::vector<std::string> symbols;
    HttpResult result = client->getWhenFree(tradingUrl + ASSETS_PATH, MAX_ATTEMPTS, FIRST_RETRY_WAIT);
    ok = result.ok() && result.body.is_array();
    if (!ok)
    {
        std::cerr << "ERROR - Alpaca assets request failed (" << result.status << "): " << result.error << std::endl;
        return symbols;
    }
    for (const auto& asset : result.body)
        if (isListedStock(asset))
            symbols.push_back(asset["symbol"].get<std::string>());
    std::sort(symbols.begin(), symbols.end());
    return symbols;
}
