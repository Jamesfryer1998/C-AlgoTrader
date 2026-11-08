#include "AlpacaDailyBars.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <thread>
#include "AlpacaApi.hpp"
#include "../util/MarketTime.hpp"

namespace
{
    const std::string DEFAULT_FEED = AlpacaApi::SIP_FEED;   // all exchanges
    const std::string ADJUSTMENT = "all";                    // prices adjusted for splits and dividends
    const std::string START_OF_DAY_UTC = "T00:00:00Z";

    // Retry when rate limited or the connection drops: waits 3 s, 6 s, ... between five attempts
    const int MAX_ATTEMPTS = 5;
    const std::chrono::seconds FIRST_RETRY_WAIT{3};
}

AlpacaDailyBars::AlpacaDailyBars(const json& config, const std::string& dataDir, std::shared_ptr<AlpacaClient> client)
    : client(client ? client : std::make_shared<AlpacaClient>()), dataDir(dataDir)
{
    json alpaca = config.value("alpaca", json::object());
    dataUrl = alpaca.value("data_url", AlpacaApi::DATA_URL);
    feed = alpaca.value("history_feed", DEFAULT_FEED);
}

std::string
AlpacaDailyBars::csvPath(const std::string& symbol) const
{
    return (std::filesystem::path(dataDir) / (symbol + ".csv")).string();
}

std::vector<quant::DailyBar>
AlpacaDailyBars::parseBars(const json& body, const std::string& skipDate)
{
    std::vector<quant::DailyBar> bars;
    if (!body.is_object() || !body.contains("bars") || !body["bars"].is_array())
        return bars;

    for (const auto& bar : body["bars"])
    {
        // Daily bars are stamped at midnight New York time, e.g. "2026-10-29T04:00:00Z"
        std::time_t utc = MarketTime::parseUtc(bar.value("t", ""));
        if (utc < 0)
            continue;
        quant::DailyBar b;
        b.date = MarketTime::datePart(MarketTime::toNewYorkTime(utc));
        if (b.date == skipDate)
            continue;
        b.open = bar.value("o", 0.0);
        b.high = bar.value("h", 0.0);
        b.low = bar.value("l", 0.0);
        b.close = bar.value("c", 0.0);
        b.volume = bar.value("v", 0.0);
        if (b.close > 0)
            bars.push_back(b);
    }
    return bars;
}

bool
AlpacaDailyBars::download(const std::string& symbol, const std::string& startDate, const std::string& endDate,
                          std::vector<quant::DailyBar>& bars)
{
    if (!client->hasKeys())
    {
        std::cerr << "WARNING - Alpaca keys missing (.env), can't download " << symbol << std::endl;
        return false;
    }

    std::time_t now = std::time(nullptr);
    std::string end = MarketTime::formatUtc(now - AlpacaApi::SIP_DELAY_SECONDS);
    if (!endDate.empty())
    {
        std::time_t requested = MarketTime::parseDate(endDate) + MarketTime::SECONDS_PER_DAY;   // include that whole day
        if (requested > 0 && requested < now - AlpacaApi::SIP_DELAY_SECONDS)
            end = MarketTime::formatUtc(requested);
    }
    std::string today = MarketTime::datePart(MarketTime::toNewYorkTime(now));

    std::string pageToken;
    bars.clear();
    do
    {
        std::string url = AlpacaApi::barsUrl(dataUrl, symbol, AlpacaApi::ONE_DAY) + "&adjustment=" + ADJUSTMENT
                        + "&limit=" + std::to_string(AlpacaApi::MAX_BARS_PER_REQUEST) + "&sort=asc&feed=" + feed
                        + "&start=" + startDate + START_OF_DAY_UTC + "&end=" + end;
        if (!pageToken.empty())
            url += "&page_token=" + pageToken;

        HttpResult result = client->getWhenFree(url, MAX_ATTEMPTS, FIRST_RETRY_WAIT);
        if (!result.ok())
        {
            std::cerr << "WARNING - Could not download " << symbol << " daily bars (" << result.status << "): "
                      << result.error << std::endl;
            return false;
        }

        std::vector<quant::DailyBar> page = parseBars(result.body, today);
        bars.insert(bars.end(), page.begin(), page.end());
        pageToken = AlpacaApi::nextPageToken(result.body);
    } while (!pageToken.empty());

    return !bars.empty();
}

std::vector<quant::DailyBar>
AlpacaDailyBars::load(const std::string& symbol, const std::string& startDate, const std::string& endDate,
                      bool offline)
{
    std::vector<quant::DailyBar> bars;
    std::string path = csvPath(symbol);

    if (!offline && download(symbol, startDate, endDate, bars))
    {
        std::filesystem::create_directories(dataDir);
        if (!quant::writeDailyCsv(path, bars))
            std::cerr << "WARNING - Could not save " << path << std::endl;
        std::cout << "Downloaded " << symbol << ": " << bars.size() << " days (" << bars.front().date
                  << " to " << bars.back().date << ")" << std::endl;
        return bars;
    }

    bars = quant::readDailyCsv(path);
    if (!bars.empty())
        std::cout << (offline ? "Using saved " : "Using saved (download failed) ") << symbol << ": "
                  << bars.size() << " days (" << bars.front().date << " to " << bars.back().date << ")" << std::endl;
    return bars;
}

bool
AlpacaDailyBars::loadAll(const std::vector<std::string>& symbols, const std::string& startDate,
                         const std::string& endDate, bool offline,
                         std::map<std::string, std::vector<quant::DailyBar>>& out)
{
    for (const auto& symbol : symbols)
    {
        out[symbol] = load(symbol, startDate, endDate, offline);
        if (out[symbol].empty())
        {
            std::cerr << "ERROR - No daily data for " << symbol << ". Check the symbol, your .env keys, or run "
                      << "without --offline to download it." << std::endl;
            return false;
        }
    }
    return true;
}
