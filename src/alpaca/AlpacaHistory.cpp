#include "AlpacaHistory.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include "../util/MarketTime.hpp"

AlpacaHistory::AlpacaHistory(const json& config)
{
    json alpaca = config.value("alpaca", json::object());
    symbol = config.at("ticker").get<std::string>();
    feed = alpaca.value("history_feed", "sip");
    dataUrl = alpaca.value("data_url", "https://data.alpaca.markets");
    fileBaseName = config.value("baseDataFileName", "marketData");
    regularHoursOnly = alpaca.value("history_regular_hours_only", true);
}

std::string
AlpacaHistory::csvPath(const std::string& dataDir, const std::string& date) const
{
    return (std::filesystem::path(dataDir) / (fileBaseName + "_" + symbol + "_" + date + ".csv")).string();
}

// A day's file is complete if its last bar is the 15:59 close (or later, with extended hours).
// Partial days (e.g. downloaded while the market was open) get downloaded again.
static bool isCompleteDay(const std::string& path)
{
    std::ifstream file(path);
    std::string line, last;
    while (std::getline(file, line))
        if (!line.empty())
            last = line;
    return last.size() >= 16 && last.substr(11, 5) >= "15:59";
}

std::vector<MarketCondition>
AlpacaHistory::fetchBars(const std::string& startUtc, const std::string& endUtc, bool& ok)
{
    std::vector<MarketCondition> bars;
    std::string pageToken;
    ok = true;

    do
    {
        std::string url = dataUrl + "/v2/stocks/" + symbol + "/bars"
                        + "?timeframe=1Min&adjustment=raw&limit=10000&sort=asc"
                        + "&feed=" + feed + "&start=" + startUtc + "&end=" + endUtc;
        if (!pageToken.empty())
            url += "&page_token=" + pageToken;

        HttpResult result = client.get(url);
        if (!result.ok() || !result.body.is_object())
        {
            std::cerr << "ERROR - Alpaca history request failed (" << result.status << "): " << result.error << std::endl;
            ok = false;
            return bars;
        }

        if (result.body.contains("bars") && result.body["bars"].is_array())
        {
            for (const auto& bar : result.body["bars"])
            {
                std::time_t utc = MarketTime::parseUtc(bar.value("t", ""));
                if (utc < 0)
                    continue;
                bars.emplace_back(MarketTime::toNewYorkTime(utc), symbol,
                                  bar.value("o", 0.0f), bar.value("c", 0.0f), bar.value("v", 0), "1m");
            }
        }

        pageToken = result.body.value("next_page_token", json()).is_string()
                  ? result.body["next_page_token"].get<std::string>() : "";
    } while (!pageToken.empty());

    return bars;
}

bool
AlpacaHistory::writeCsv(const std::string& path, const std::vector<MarketCondition>& bars)
{
    std::ofstream file(path);
    if (!file)
        return false;

    file << "Datetime,Ticker,Open,Close,Volume,TimeInterval\n";
    file << std::fixed << std::setprecision(4);
    for (const auto& bar : bars)
        file << bar.DateTime << "," << bar.Ticker << "," << bar.Open << "," << bar.Close << ","
             << bar.Volume << "," << bar.TimeInterval << "\n";
    return true;
}

int
AlpacaHistory::downloadToCsv(const std::string& startDate, const std::string& endDate,
                             const std::string& dataDir, bool overwrite)
{
    std::time_t startDay = MarketTime::parseDate(startDate);
    std::time_t endDay = MarketTime::parseDate(endDate);
    if (startDay < 0 || endDay < 0 || endDay < startDay)
    {
        std::cerr << "ERROR - Bad date range " << startDate << " to " << endDate << " (use YYYY-MM-DD)" << std::endl;
        return -1;
    }

    // A New York day runs 04:00/05:00 UTC to 04:00/05:00 UTC the next day, so ask for a little extra
    // either side and keep only bars whose New York date is in range.
    std::time_t requestStart = startDay;
    std::time_t requestEnd = endDay + 32 * 3600;

    // The free plan can't query the most recent 15 minutes of SIP data
    std::time_t latestAllowed = std::time(nullptr) - 16 * 60;
    if (requestEnd > latestAllowed)
        requestEnd = latestAllowed;

    std::cout << "Downloading " << symbol << " 1-minute bars (" << feed << " feed) "
              << startDate << " to " << endDate << " from Alpaca..." << std::endl;

    bool ok = false;
    std::vector<MarketCondition> bars = fetchBars(MarketTime::formatUtc(requestStart),
                                                  MarketTime::formatUtc(requestEnd), ok);
    if (!ok)
        return -1;

    // Group by New York trading day
    std::map<std::string, std::vector<MarketCondition>> byDay;
    for (const auto& bar : bars)
    {
        std::string day = MarketTime::datePart(bar.DateTime);
        if (day < startDate || day > endDate)
            continue;
        if (regularHoursOnly && !MarketTime::isRegularSession(bar.DateTime))
            continue;
        byDay[day].push_back(bar);
    }

    std::filesystem::create_directories(dataDir);

    int written = 0;
    for (const auto& [day, dayBars] : byDay)
    {
        std::string path = csvPath(dataDir, day);
        if (!overwrite && std::filesystem::exists(path) && isCompleteDay(path))
        {
            std::cout << "  " << day << ": already downloaded, skipping" << std::endl;
            continue;
        }
        if (!writeCsv(path, dayBars))
        {
            std::cerr << "ERROR - Could not write " << path << std::endl;
            continue;
        }
        std::cout << "  " << day << ": " << dayBars.size() << " bars -> " << path << std::endl;
        written += dayBars.size();
    }

    if (byDay.empty())
        std::cout << "  No bars returned (weekend, holiday, or dates in the future?)" << std::endl;

    return written;
}
