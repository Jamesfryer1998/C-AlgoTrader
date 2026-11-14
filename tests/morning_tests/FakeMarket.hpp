#pragma once

#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "alpaca/AlpacaClient.hpp"
#include "util/MarketTime.hpp"

/**
 * FakeMarket
 *
 * A pretend Alpaca for the morning screen's tests, answering from what each request asks for:
 *
 *   /v2/assets              the symbols given
 *   timeframe=1Day          a daily bar per weekday in the range (close 50, range 2, 2 million shares)
 *   timeframe=5Min          an opening bar at the start time, per symbol: `opening` decides it
 *   timeframe=1Min          a whole session from the start time, per symbol: `session` decides each minute
 *
 * Every request is recorded.
 */
class FakeMarket : public AlpacaClient
{
    public:
        // (symbol, minutes after 09:30) -> {open, high, low, close}
        using Session = std::function<std::vector<double>(const std::string&, int)>;

        explicit FakeMarket(std::vector<std::string> symbols) : AlpacaClient("PKTEST", "SECRET"), symbols(symbols) {}

        HttpResult get(const std::string& url) override
        {
            requests.push_back(url);
            HttpResult result;
            result.status = 200;
            if (url.find("/v2/assets") != std::string::npos)
            {
                result.body = json::array();
                for (const auto& symbol : symbols)
                    result.body.push_back({{"symbol", symbol}, {"exchange", "NASDAQ"}, {"tradable", true},
                                           {"status", "active"}});
                return result;
            }
            std::time_t start = MarketTime::parseUtc(parameter(url, "start"));
            std::time_t end = MarketTime::parseUtc(parameter(url, "end"));
            std::string timeframe = parameter(url, "timeframe");
            json bars = json::object();
            for (const auto& symbol : split(parameter(url, "symbols")))
            {
                json list = json::array();
                if (timeframe == "1Day")
                    for (std::time_t day = start; day <= end; day += MarketTime::SECONDS_PER_DAY)
                    {
                        std::string date = MarketTime::datePart(MarketTime::formatUtc(day));
                        if (MarketTime::isWeekday(date))
                            list.push_back(barJson(day + DAILY_BAR_HOUR, 50, 51, 49, 50, DAILY_VOLUME));
                    }
                else if (timeframe == "5Min")
                {
                    std::vector<double> prices = opening(symbol);
                    list.push_back(barJson(start, prices[0], prices[1], prices[2], prices[3], prices[4]));
                }
                else
                    for (int minute = 0; minute < MarketTime::MINUTES_PER_SESSION; minute++)
                    {
                        std::vector<double> prices = session(symbol, minute);
                        list.push_back(barJson(start + minute * SECONDS_PER_MINUTE, prices[0], prices[1], prices[2],
                                               prices[3], MINUTE_VOLUME));
                    }
                if (!list.empty())
                    bars[symbol] = list;
            }
            result.body = {{"bars", bars}, {"next_page_token", nullptr}};
            return result;
        }

        size_t count(const std::string& part) const
        {
            size_t found = 0;
            for (const auto& url : requests)
                found += url.find(part) != std::string::npos;
            return found;
        }

        std::vector<std::string> symbols;
        std::vector<std::string> requests;
        // symbol -> {open, high, low, close, volume} of its first 5 minutes
        std::function<std::vector<double>(const std::string&)> opening = [](const std::string&) {
            return std::vector<double>{50, 51, 50, 51, 1000};
        };
        Session session = [](const std::string&, int) { return std::vector<double>{50, 50, 50, 50}; };

    private:
        static constexpr std::time_t SECONDS_PER_MINUTE = 60;
        static constexpr std::time_t DAILY_BAR_HOUR = 5 * 60 * 60;   // Alpaca stamps a daily bar at New York midnight
        static constexpr double DAILY_VOLUME = 2e6;
        static constexpr double MINUTE_VOLUME = 1000;

        static json barJson(std::time_t time, double open, double high, double low, double close, double volume)
        {
            return {{"t", MarketTime::formatUtc(time)}, {"o", open}, {"h", high}, {"l", low}, {"c", close}, {"v", volume}};
        }

        static std::string parameter(const std::string& url, const std::string& name)
        {
            size_t at = url.find(name + "=");
            if (at == std::string::npos)
                return "";
            size_t from = at + name.size() + 1, to = url.find('&', from);
            return url.substr(from, to == std::string::npos ? std::string::npos : to - from);
        }

        static std::vector<std::string> split(const std::string& joined)
        {
            std::vector<std::string> parts;
            std::stringstream stream(joined);
            std::string part;
            while (std::getline(stream, part, ','))
                parts.push_back(part);
            return parts;
        }
};
