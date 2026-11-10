#include "AlpacaHistory.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <chrono>
#include <thread>
#include "AlpacaApi.hpp"
#include "../util/MarketTime.hpp"
#include "../util/SaveData.hpp"

namespace
{
    // Used when config/general.json doesn't say
    const std::string DEFAULT_FEED = AlpacaApi::SIP_FEED;   // all exchanges: real volumes
    const std::string DEFAULT_ADJUSTMENT = "split";          // prices adjusted for stock splits

    // Retry when rate limited or the connection drops: waits 5 s, 10 s, ... between six attempts
    const int MAX_ATTEMPTS = 6;
    const std::chrono::seconds FIRST_RETRY_WAIT{5};

    // A New York day runs 04:00/05:00 UTC to 04:00/05:00 UTC the next day, so each month's request
    // reaches this far past its last day; bars outside the month are dropped afterwards
    const std::time_t REQUEST_OVERLAP_SECONDS = 32 * MarketTime::SECONDS_PER_HOUR;

    // A day's file is complete once it reaches the last bar of the session...
    const std::string LAST_BAR_TIME = "15:59";
    const size_t TIME_START = MarketTime::DATE_LENGTH + 1;   // "YYYY-MM-DD HH:MM"
    const size_t HOURS_AND_MINUTES = 5;

    const int PRICE_DECIMALS = 4;

    // data/.alpaca_history_<TICKER>.json remembers which months are finished
    const std::string PROGRESS_FILE_PREFIX = ".alpaca_history_";
    const char* const MONTH_FORMAT = "%04d-%02d";   // "2026-10"
    const std::string FIRST_OF_MONTH = "-01";
    const int DECEMBER = 12;
    const size_t MONTH_LENGTH = 7;                  // "2026-10"
}

AlpacaHistory::AlpacaHistory(const json& config, std::shared_ptr<AlpacaClient> client)
    : client(client ? client : std::make_shared<AlpacaClient>())
{
    json alpaca = config.value("alpaca", json::object());
    symbol = config.at("ticker").get<std::string>();
    feed = alpaca.value("history_feed", DEFAULT_FEED);
    dataUrl = alpaca.value("data_url", AlpacaApi::DATA_URL);
    fileBaseName = config.value("baseDataFileName", DEFAULT_MARKET_DATA_FILE_NAME);
    adjustment = alpaca.value("history_adjustment", DEFAULT_ADJUSTMENT);
    regularHoursOnly = alpaca.value("history_regular_hours_only", true);
}

std::string
AlpacaHistory::csvPath(const std::string& dataDir, const std::string& date) const
{
    return (std::filesystem::path(dataDir) / (fileBaseName + "_" + symbol + "_" + date + CSV_EXTENSION)).string();
}

// When the file was last written, as UTC seconds
static std::time_t writtenAt(const std::string& path)
{
    std::error_code error;
    auto written = std::filesystem::last_write_time(path, error);
    if (error)
        return -1;
    auto age = std::chrono::duration_cast<std::chrono::seconds>(std::filesystem::file_time_type::clock::now() - written);
    return std::time(nullptr) - age.count();
}

bool
AlpacaHistory::isCompleteDay(const std::string& path, const std::string& day)
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
    if (first.size() < TIME_START + HOURS_AND_MINUTES || MarketTime::datePart(first) != day)
        return false;
    if (last.substr(TIME_START, HOURS_AND_MINUTES) >= LAST_BAR_TIME)
        return true;
    // ...or once it was written after that day's close, when Alpaca had all of it (an early close, a halted stock)
    std::time_t dataComplete = MarketTime::regularCloseAfter(MarketTime::parseDate(day)) + AlpacaApi::SIP_DELAY_SECONDS;
    return writtenAt(path) >= dataComplete;
}

std::vector<MarketCondition>
AlpacaHistory::fetchBars(const std::string& startUtc, const std::string& endUtc, bool& ok)
{
    std::vector<MarketCondition> bars;
    std::string pageToken;
    ok = true;

    do
    {
        std::string url = AlpacaApi::barsUrl(dataUrl, symbol, AlpacaApi::ONE_MINUTE)
                        + "&adjustment=" + adjustment + "&limit=" + std::to_string(AlpacaApi::MAX_BARS_PER_REQUEST)
                        + "&sort=asc&feed=" + feed + "&start=" + startUtc + "&end=" + endUtc;
        if (!pageToken.empty())
            url += "&page_token=" + pageToken;

        HttpResult result = client->getWhenFree(url, MAX_ATTEMPTS, FIRST_RETRY_WAIT);

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
                MarketCondition parsed = AlpacaApi::parseMinuteBar(bar, symbol);
                if (!parsed.DateTime.empty())
                    bars.push_back(parsed);
            }
        }

        pageToken = AlpacaApi::nextPageToken(result.body);
    } while (!pageToken.empty());

    return bars;
}

bool
AlpacaHistory::writeCsv(const std::string& path, const std::vector<MarketCondition>& bars)
{
    {
        std::ofstream file(path);
        if (!file)
            return false;

        file << MARKET_DATA_CSV_HEADER << "\n";
        file << std::fixed << std::setprecision(PRICE_DECIMALS);
        for (const auto& bar : bars)
            file << bar.DateTime << "," << bar.Ticker << "," << bar.Open << "," << bar.Close << ","
                 << bar.Volume << "," << bar.TimeInterval << "\n";
    }
    SaveData::saved(path);
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
    std::string progressPath = (std::filesystem::path(dataDir) / (PROGRESS_FILE_PREFIX + symbol + ".json")).string();
    std::set<std::string> doneMonths;
    {
        std::ifstream progressFile(progressPath);
        json progress = json::parse(progressFile, nullptr, false);
        if (progress.is_object() && progress.contains("months") && progress.value("feed", "") == feed
            && progress.value("adjustment", "") == adjustment)
            for (const auto& month : progress["months"])
                doneMonths.insert(month.get<std::string>());
    }

    if (!quiet)
        std::cout << "Downloading " << symbol << " 1-minute bars (" << feed << " feed, " << adjustment << "-adjusted) "
              << startDate << " to " << endDate << " from Alpaca, a month at a time..." << std::endl;

    int written = 0;
    std::tm tm{};
    gmtime_r(&startDay, &tm);
    int year = tm.tm_year + 1900, month = tm.tm_mon + 1;

    while (true)
    {
        char monthKey[32];
        std::snprintf(monthKey, sizeof(monthKey), MONTH_FORMAT, year, month);
        std::time_t monthStart = MarketTime::parseDate(std::string(monthKey) + FIRST_OF_MONTH);
        if (monthStart > endDay)
            break;
        int nextYear = month == DECEMBER ? year + 1 : year, nextMonth = month == DECEMBER ? 1 : month + 1;
        char nextKey[32];
        std::snprintf(nextKey, sizeof(nextKey), MONTH_FORMAT, nextYear, nextMonth);
        std::time_t monthEnd = MarketTime::parseDate(std::string(nextKey) + FIRST_OF_MONTH) - MarketTime::SECONDS_PER_DAY;

        if (!overwrite && doneMonths.count(monthKey))
        {
            if (!quiet)
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
                SaveData::saved(progressPath);
            }
        }

        year = nextYear;
        month = nextMonth;
    }

    if (!quiet)
        std::cout << "Done: " << written << " new bars written to " << dataDir << std::endl;
    return written;
}

int
AlpacaHistory::downloadChunk(std::time_t chunkStart, std::time_t chunkEnd, const std::string& startDate,
                             const std::string& endDate, const std::string& dataDir, bool overwrite)
{
    std::time_t requestStart = chunkStart;
    std::time_t requestEnd = chunkEnd + REQUEST_OVERLAP_SECONDS;

    std::time_t latestAllowed = std::time(nullptr) - AlpacaApi::SIP_DELAY_SECONDS;
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
        if (!overwrite && std::filesystem::exists(path) && isCompleteDay(path, day))
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

    if (!quiet)
        std::cout << "  " << firstDay.substr(0, MONTH_LENGTH) << ": " << byDay.size() << " trading days, "
              << written << " bars written" << (skipped ? ", " + std::to_string(skipped) + " days already saved" : "")
              << std::endl;
    return written;
}
