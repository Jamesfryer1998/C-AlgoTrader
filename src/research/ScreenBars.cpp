#include "ScreenBars.hpp"

#include "../alpaca/AlpacaHistory.hpp"
#include "../backtest/MinuteBars.hpp"
#include "../backtest/Screener.hpp"

namespace
{
    // Under data/: daily bars in daily/<SYMBOL>.csv, minute bars in minute/<SYMBOL>/ (one CSV per day)
    const std::string DAILY_FOLDER = "daily";
    const std::string MINUTE_FOLDER = "minute/";
}

ScreenBars::ScreenBars(const json& baseConfig, const std::string& dataRoot, const ScreenSettings& screen, int threads)
    : baseConfig(baseConfig), dataRoot(dataRoot), screen(screen), threads(threads),
      dailySource(baseConfig, dataRoot + DAILY_FOLDER)
{
}

bool
ScreenBars::download(const std::string& symbol)
{
    if (screen.daily)
        return !dailySource.load(symbol, screen.start, "", false).empty();

    json stockConfig = baseConfig;
    stockConfig["ticker"] = symbol;
    AlpacaHistory history(stockConfig);
    history.setQuiet(true);
    return history.downloadToCsv(screen.start, screen.end, minuteFolder(symbol)) >= 0;
}

std::vector<MarketCondition>
ScreenBars::load(const std::string& symbol)
{
    if (screen.daily)
        return Screener::toBars(symbol, dailySource.load(symbol, screen.start, "", true));
    return MinuteBars::load(minuteFolder(symbol), symbol, screen.start, screen.end, threads);
}

std::string
ScreenBars::minuteFolder(const std::string& symbol) const
{
    return dataRoot + MINUTE_FOLDER + symbol;
}
