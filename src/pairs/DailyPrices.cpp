#include "DailyPrices.hpp"

#include <filesystem>
#include <iostream>
#include "../morning/MorningData.hpp"

namespace fs = std::filesystem;

namespace
{
    const std::string DAILY_STORE = "pairs";             // under data/: where the shared daily bars live
    const std::string MORNING_FOLDER = "morning";        // the morning screen's downloads
    const std::string SYMBOLS_FILE = "symbols.txt";
    const std::string DAILY_FOLDER = "daily";
    const std::string CSV = ".csv";
    const std::string STAGE_DAILY = "Daily bars";
    const std::string STAGE_PRICES = "Lining up prices";

    double averageDollarVolume(const std::vector<quant::DailyBar>& bars)
    {
        double dollars = 0;
        for (const auto& bar : bars)
            dollars += bar.close * bar.volume;
        return bars.empty() ? 0 : dollars / bars.size();
    }

    // Those averaging at least `above` a day in the morning screen's daily bars, if it has some; else all
    std::vector<std::string> worthDownloading(const std::string& dataRoot, const std::vector<std::string>& symbols,
                                              double above)
    {
        fs::path morningDaily = fs::path(dataRoot) / MORNING_FOLDER / DAILY_FOLDER;
        if (!fs::is_directory(morningDaily))
            return symbols;
        std::vector<std::string> kept;
        for (const auto& symbol : symbols)
            if (symbol == DailyPrices::BENCHMARK
                || averageDollarVolume(quant::readDailyCsv((morningDaily / (symbol + CSV)).string())) >= above)
                kept.push_back(symbol);
        return kept;
    }
}

namespace DailyPrices
{
    bool load(const json& algoConfig, const std::string& dataRoot, const Request& request, ResearchProgress& progress,
              PriceMatrix& prices, size_t& downloads)
    {
        std::string store = (fs::path(dataRoot) / DAILY_STORE).string();
        fs::create_directories(store);
        fs::path morningSymbols = fs::path(dataRoot) / MORNING_FOLDER / SYMBOLS_FILE;
        fs::path symbolsFile = fs::path(store) / SYMBOLS_FILE;
        if (!fs::exists(symbolsFile) && fs::exists(morningSymbols))
            fs::copy_file(morningSymbols, symbolsFile);

        MorningData data(algoConfig, store, request.offline);
        std::vector<std::string> listed;
        if (!data.symbols(listed))
        {
            std::cerr << "ERROR - Couldn't get the list of US stocks" << std::endl;
            return false;
        }
        std::vector<std::string> symbols = worthDownloading(dataRoot, listed, request.downloadAbove);
        std::cout << symbols.size() << " of " << listed.size() << " stocks trade enough to consider" << std::endl;
        if (!data.ensureDailyBars(symbols, request.from, request.to, [&](size_t done, size_t total) {
                progress.stage(STAGE_DAILY, done, total);
            }))
            std::cerr << "WARNING - Some daily bars couldn't be downloaded; those stocks are left out" << std::endl;
        downloads = data.downloads();

        prices.days.clear();
        for (const auto& bar : data.dailyBars(BENCHMARK))
            if (bar.date >= request.from && bar.date <= request.to)
                prices.days.push_back(bar.date);
        if (prices.days.empty())
        {
            std::cerr << "ERROR - No daily bars for " << BENCHMARK << ", the trading calendar" << std::endl;
            return false;
        }
        for (size_t i = 0; i < symbols.size(); i++)
        {
            progress.stage(STAGE_PRICES, i, symbols.size());
            std::vector<quant::DailyBar> bars = data.dailyBars(symbols[i]);
            if (symbols[i] == BENCHMARK || averageDollarVolume(bars) >= request.loadAbove)
                prices.add(symbols[i], bars);
        }
        return true;
    }

    MorningBacktest::Line buyAndHold(const PriceMatrix& prices, size_t first, size_t last, double capital)
    {
        MorningBacktest::Line spy;
        spy.variant = MorningBacktest::BENCHMARK;
        spy.selection = BENCHMARK;
        spy.equity = capital;
        size_t spyRow = prices.row(BENCHMARK);
        for (size_t day = first; spyRow < prices.stocks() && day <= last && day < prices.days.size(); day++)
        {
            double change = prices.dailyReturn(spyRow, day);
            spy.days.push_back(prices.days[day]);
            spy.returns.push_back(change);
            spy.equity *= 1 + change;
        }
        return spy;
    }

    size_t indexOnOrBefore(const PriceMatrix& prices, const std::string& date)
    {
        size_t last = prices.indexOnOrAfter(date);
        if (last >= prices.days.size() || prices.days[last] > date)
            last = last == 0 ? 0 : last - 1;
        return last;
    }
}
