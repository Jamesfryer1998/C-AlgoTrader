#include "AlpacaApi.hpp"

#include "../util/MarketTime.hpp"

namespace
{
    const std::string BARS_PATH = "/bars";
    const std::string NEXT_PAGE_TOKEN = "next_page_token";
    const std::string MINUTE_INTERVAL = "1m";   // MarketCondition's name for a 1-minute bar
}

namespace AlpacaApi
{
    std::string barsUrl(const std::string& dataUrl, const std::string& symbol, const std::string& timeframe)
    {
        return dataUrl + STOCKS_PATH + symbol + BARS_PATH + "?timeframe=" + timeframe;
    }

    double numberField(const json& body, const std::string& key)
    {
        try
        {
            return body.contains(key) && body[key].is_string() ? std::stod(body[key].get<std::string>()) : 0.0;
        }
        catch (const std::exception&)
        {
            return 0.0;
        }
    }

    std::string nextPageToken(const json& body)
    {
        return body.is_object() && body.value(NEXT_PAGE_TOKEN, json()).is_string()
             ? body[NEXT_PAGE_TOKEN].get<std::string>() : "";
    }

    MarketCondition parseMinuteBar(const json& bar, const std::string& symbol)
    {
        std::time_t utc = MarketTime::parseUtc(bar.value("t", ""));
        std::string time = utc >= 0 ? MarketTime::toNewYorkTime(utc) : "";
        return MarketCondition(time, bar.value("S", symbol), bar.value("o", 0.0f), bar.value("c", 0.0f),
                               bar.value("v", 0), MINUTE_INTERVAL);
    }
}
