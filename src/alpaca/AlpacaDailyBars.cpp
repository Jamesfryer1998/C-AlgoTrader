#include "AlpacaDailyBars.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <thread>
#include "../util/MarketTime.hpp"

AlpacaDailyBars::AlpacaDailyBars(const json& config, const std::string& dataDir) : dataDir(dataDir)
{
    json alpaca = config.value("alpaca", json::object());
    dataUrl = alpaca.value("data_url", "https://data.alpaca.markets");
    feed = alpaca.value("history_feed", "sip");
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
    if (!client.hasKeys())
    {
        std::cerr << "WARNING - Alpaca keys missing (.env), can't download " << symbol << std::endl;
        return false;
    }

    std::time_t now = std::time(nullptr);
    std::string end = MarketTime::formatUtc(now - AlpacaClient::SIP_DELAY_SECONDS);
    if (!endDate.empty())
    {
        std::time_t requested = MarketTime::parseDate(endDate) + MarketTime::SECONDS_PER_DAY;   // include that whole day
        if (requested > 0 && requested < now - AlpacaClient::SIP_DELAY_SECONDS)
            end = MarketTime::formatUtc(requested);
    }
    std::string today = MarketTime::datePart(MarketTime::toNewYorkTime(now));

    std::string pageToken;
    bars.clear();
    do
    {
        std::string url = dataUrl + "/v2/stocks/" + symbol + "/bars?timeframe=1Day&adjustment=all&limit="
                        + std::to_string(AlpacaClient::MAX_BARS_PER_REQUEST)
                        + "&sort=asc&feed=" + feed + "&start=" + startDate + "T00:00:00Z&end=" + end;
        if (!pageToken.empty())
            url += "&page_token=" + pageToken;

        HttpResult result = client.getWhenFree(url, MAX_ATTEMPTS, FIRST_RETRY_WAIT);
        if (!result.ok())
        {
            std::cerr << "WARNING - Could not download " << symbol << " daily bars (" << result.status << "): "
                      << result.error << std::endl;
            return false;
        }

        std::vector<quant::DailyBar> page = parseBars(result.body, today);
        bars.insert(bars.end(), page.begin(), page.end());
        pageToken = result.body.value("next_page_token", json()).is_string()
                  ? result.body["next_page_token"].get<std::string>() : "";
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
