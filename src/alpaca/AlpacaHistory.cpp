#include "AlpacaHistory.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <chrono>
#include <thread>
#include "../util/MarketTime.hpp"

AlpacaHistory::AlpacaHistory(const json& config)
{
    json alpaca = config.value("alpaca", json::object());
    symbol = config.at("ticker").get<std::string>();
    feed = alpaca.value("history_feed", "sip");
    dataUrl = alpaca.value("data_url", "https://data.alpaca.markets");
    fileBaseName = config.value("baseDataFileName", "marketData");
    adjustment = alpaca.value("history_adjustment", "split");
    regularHoursOnly = alpaca.value("history_regular_hours_only", true);
}

std::string
AlpacaHistory::csvPath(const std::string& dataDir, const std::string& date) const
{
    return (std::filesystem::path(dataDir) / (fileBaseName + "_" + symbol + "_" + date + ".csv")).string();
}

// Is <path> a finished download of <day>? Its first bar must be from that day (old Yahoo files shared these
// names but held a different week). Past days are always written whole; today's may be partial, so it
// counts as finished only once it reaches the 15:59 bar.
static bool isCompleteDay(const std::string& path, const std::string& day, const std::string& today)
{
    std::ifstream file(path);
    std::string line, first, last;
    std::getline(file, line); // header
    while (std::getline(file, line))
    {
        if (line.empty())
            continue;
        if (first.empty())
            first = line;
        last = line;
    }
    if (first.size() < 16 || first.substr(0, 10) != day)
        return false;
    return day < today || last.substr(11, 5) >= "15:59";
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
                        + "?timeframe=1Min&adjustment=" + adjustment + "&limit=10000&sort=asc"
                        + "&feed=" + feed + "&start=" + startUtc + "&end=" + endUtc;
        if (!pageToken.empty())
            url += "&page_token=" + pageToken;

        // Retry when rate limited (HTTP 429) or the connection drops, waiting a little longer each time
        HttpResult result;
        for (int attempt = 1; attempt <= 6; attempt++)
        {
            result = client.get(url);
            bool retry = result.status == 429 || result.status == 0 || result.status >= 500;
            if (!retry)
                break;
            int waitSeconds = 5 * attempt;
            std::cerr << "  Alpaca busy (" << result.status << "), retrying in " << waitSeconds << "s..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(waitSeconds));
        }

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

    std::filesystem::create_directories(dataDir);
    std::string today = MarketTime::datePart(MarketTime::toNewYorkTime(std::time(nullptr)));

    // Months already downloaded in full (only months that have completely ended get recorded)
    std::string progressPath = (std::filesystem::path(dataDir) / (".alpaca_history_" + symbol + ".json")).string();
    std::set<std::string> doneMonths;
    {
        std::ifstream progressFile(progressPath);
        json progress = json::parse(progressFile, nullptr, false);
        if (progress.is_object() && progress.contains("months") && progress.value("feed", "") == feed
            && progress.value("adjustment", "") == adjustment)
            for (const auto& month : progress["months"])
                doneMonths.insert(month.get<std::string>());
    }

    std::cout << "Downloading " << symbol << " 1-minute bars (" << feed << " feed, " << adjustment << "-adjusted) "
              << startDate << " to " << endDate << " from Alpaca, a month at a time..." << std::endl;

    int written = 0;
    std::tm tm{};
    gmtime_r(&startDay, &tm);
    int year = tm.tm_year + 1900, month = tm.tm_mon + 1;

    while (true)
    {
        char monthKey[32];
        std::snprintf(monthKey, sizeof(monthKey), "%04d-%02d", year, month);
        std::time_t monthStart = MarketTime::parseDate(std::string(monthKey) + "-01");
        if (monthStart > endDay)
            break;
        int nextYear = month == 12 ? year + 1 : year, nextMonth = month == 12 ? 1 : month + 1;
        char nextKey[32];
        std::snprintf(nextKey, sizeof(nextKey), "%04d-%02d", nextYear, nextMonth);
        std::time_t monthEnd = MarketTime::parseDate(std::string(nextKey) + "-01") - 24 * 3600; // last day

        if (!overwrite && doneMonths.count(monthKey))
        {
            std::cout << "  " << monthKey << ": already downloaded" << std::endl;
        }
        else
        {
            int count = downloadChunk(std::max(monthStart, startDay), std::min(monthEnd, endDay),
                                      startDate, endDate, dataDir, overwrite);
            if (count < 0)
                return -1;
            written += count;

            // Remember months that are fully in the past and fully inside the requested range
            bool monthOver = MarketTime::datePart(MarketTime::formatUtc(monthEnd)) < today;
            if (monthOver && monthStart >= startDay && monthEnd <= endDay)
            {
                doneMonths.insert(monthKey);
                json progress = {{"feed", feed}, {"adjustment", adjustment}, {"months", doneMonths}};
                std::ofstream(progressPath) << progress.dump(2) << "\n";
            }
        }

        year = nextYear;
        month = nextMonth;
    }

    std::cout << "Done: " << written << " new bars written to " << dataDir << std::endl;
    return written;
}

int
AlpacaHistory::downloadChunk(std::time_t chunkStart, std::time_t chunkEnd, const std::string& startDate,
                             const std::string& endDate, const std::string& dataDir, bool overwrite)
{
    std::string today = MarketTime::datePart(MarketTime::toNewYorkTime(std::time(nullptr)));

    // A New York day runs 04:00/05:00 UTC to 04:00/05:00 UTC the next day, so ask for a little extra
    // either side and keep only bars whose New York date is in range.
    std::time_t requestStart = chunkStart;
    std::time_t requestEnd = chunkEnd + 32 * 3600;

    // The free plan can't query the most recent 15 minutes of SIP data
    std::time_t latestAllowed = std::time(nullptr) - 16 * 60;
    if (requestEnd > latestAllowed)
        requestEnd = latestAllowed;
    if (requestEnd <= requestStart)
        return 0;

    bool ok = false;
    std::vector<MarketCondition> bars = fetchBars(MarketTime::formatUtc(requestStart),
                                                  MarketTime::formatUtc(requestEnd), ok);
    if (!ok)
        return -1;

    std::string firstDay = MarketTime::datePart(MarketTime::formatUtc(chunkStart));
    std::string lastDay = MarketTime::datePart(MarketTime::formatUtc(chunkEnd));

    // Group by New York trading day
    std::map<std::string, std::vector<MarketCondition>> byDay;
    for (const auto& bar : bars)
    {
        std::string day = MarketTime::datePart(bar.DateTime);
        if (day < firstDay || day > lastDay || day < startDate || day > endDate)
            continue;
        if (regularHoursOnly && !MarketTime::isRegularSession(bar.DateTime))
            continue;
        byDay[day].push_back(bar);
    }

    int written = 0, skipped = 0;
    for (const auto& [day, dayBars] : byDay)
    {
        std::string path = csvPath(dataDir, day);
        if (!overwrite && std::filesystem::exists(path) && isCompleteDay(path, day, today))
        {
            skipped++;
            continue;
        }
        if (!writeCsv(path, dayBars))
        {
            std::cerr << "ERROR - Could not write " << path << std::endl;
            continue;
        }
        written += dayBars.size();
    }

    std::cout << "  " << firstDay.substr(0, 7) << ": " << byDay.size() << " trading days, "
              << written << " bars written" << (skipped ? ", " + std::to_string(skipped) + " days already saved" : "")
              << std::endl;
    return written;
}
