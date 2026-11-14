#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "MorningSettings.hpp"
#include "../alpaca/AlpacaMultiBars.hpp"
#include "../data_access/IntradayBar.hpp"
#include "../quant/DailyBars.hpp"

using json = nlohmann::json;

/**
 * MorningData
 *
 * The bars the morning screen needs, downloaded from Alpaca once and kept under data/morning/:
 *
 *   symbols.txt              the US stocks and ETFs Alpaca trades (the universe before any filter)
 *   daily/<SYMBOL>.csv       daily bars (high, low and volume for the filters and the ATR)
 *   opening/<DAY>.csv        each universe stock's first 5-minute bar that day (its relative volume)
 *   minute/<DAY>.csv         1-minute bars, only for the stocks picked that day and the random control
 *
 * So three years cost a few thousand requests and about a gigabyte, not every minute of every stock.
 * Offline, nothing is downloaded: whatever is missing is missing.
 */
class MorningData
{
    public:
        using BarsBySymbol = AlpacaMultiBars::BarsBySymbol;

        // folder: data/morning; config: the merged config (Alpaca's URLs and feed)
        MorningData(const json& config, const std::string& folder, bool offline,
                    std::shared_ptr<AlpacaClient> client = nullptr);

        // The universe's symbols (from symbols.txt, else downloaded and saved). False if neither works.
        bool symbols(std::vector<std::string>& out);

        // Make sure every symbol has daily bars from `from` to `to`, downloading those that don't.
        // `progress(done, total)` is called as batches finish. False if a download failed.
        bool ensureDailyBars(const std::vector<std::string>& symbols, const std::string& from, const std::string& to,
                             const std::function<void(size_t, size_t)>& progress);

        std::vector<quant::DailyBar> dailyBars(const std::string& symbol) const;

        // Each symbol's first opening_minutes bar on `day` (only those that traded). Saved per day.
        std::map<std::string, IntradayBar> openingBars(const std::string& day, const std::vector<std::string>& symbols,
                                                       int openingMinutes);

        // 1-minute bars for the regular session of `day`. Saved per day; symbols asked for before are not
        // asked for again, even if Alpaca had nothing for them.
        BarsBySymbol minuteBars(const std::string& day, const std::vector<std::string>& symbols);

        // How many requests went to Alpaca (each batch counts once, not its pages)
        size_t downloads() const { return requests; }

    private:
        std::string dailyPath(const std::string& symbol) const;
        std::string openingPath(const std::string& day) const;
        std::string minutePath(const std::string& day) const;

        static std::map<std::string, IntradayBar> readOpenings(const std::string& path);
        static bool writeOpenings(const std::string& path, const std::map<std::string, IntradayBar>& bars);
        static BarsBySymbol readMinutes(const std::string& path, std::vector<std::string>& asked);
        static bool writeMinutes(const std::string& path, const BarsBySymbol& bars, const std::vector<std::string>& asked);

        AlpacaMultiBars alpaca;
        std::string folder;
        bool offline;
        size_t requests = 0;
};
