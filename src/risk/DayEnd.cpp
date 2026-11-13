#include "DayEnd.hpp"

#include <cmath>
#include "../util/MarketTime.hpp"

namespace
{
    const std::string SETTINGS = "end_of_day";
    const std::string FLAT_AT_CLOSE = "flat_at_close";
    const std::string NO_ENTRIES_MINUTES = "no_entries_minutes_before_close";
    const std::string CLOSE_MINUTES = "close_minutes_before_close";

    // The risk checks want a stop loss and take profit on every order; a closing order leaves nothing for
    // them to protect
    const float CLOSING_PROTECTION_PERCENT = 5;
}

DayEnd::DayEnd(const json& config)
{
    if (!config.contains(SETTINGS) || !config.at(SETTINGS).is_object())
        return;
    const json& settings = config.at(SETTINGS);
    flatAtClose = settings.value(FLAT_AT_CLOSE, false);
    noEntriesMinutes = settings.value(NO_ENTRIES_MINUTES, DEFAULT_NO_ENTRIES_MINUTES);
    closeMinutes = settings.value(CLOSE_MINUTES, DEFAULT_CLOSE_MINUTES);
}

DayEnd::Phase
DayEnd::phaseAt(int minute, int closeMinute) const
{
    if (!flatAtClose || minute < 0)
        return Phase::Trading;
    if (minute >= closeMinute - closeMinutes)
        return Phase::Close;
    if (minute >= closeMinute - noEntriesMinutes)
        return Phase::NoNewEntries;
    return Phase::Trading;
}

DayEnd::Phase
DayEnd::phaseOfBar(const std::string& newYorkDateTime) const
{
    return phaseOfBar(newYorkDateTime, MarketTime::CLOSE_MINUTE);
}

DayEnd::Phase
DayEnd::phaseOfBar(const std::string& newYorkDateTime, int closeMinute) const
{
    if (!flatAtClose || !MarketTime::isRegularSession(newYorkDateTime))
        return Phase::Trading;
    return phaseAt(MarketTime::minuteOfDay(newYorkDateTime) + 1, closeMinute);   // +1: when the bar ends
}

bool
DayEnd::opensOrAdds(const Order& order, float held)
{
    return order.isBuy() ? held >= 0 : held <= 0;
}

Order
DayEnd::closingOrder(const std::string& ticker, float held, float price)
{
    Order close(held > 0 ? OrderType::SELL : OrderType::BUY, ticker, std::fabs(held), price);
    close.setStopLoss(CLOSING_PROTECTION_PERCENT);
    close.setTakeProfit(CLOSING_PROTECTION_PERCENT);
    close.setStrategy(STRATEGY);
    return close;
}
