#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "AlpacaClient.hpp"
#include "../data_access/IntradayBar.hpp"

using json = nlohmann::json;

/**
 * AlpacaMultiBars
 *
 * Bars for many stocks at once, and the list of US stocks Alpaca trades, for research across hundreds of
 * stocks (the morning screen). One request asks for up to `symbolsPerRequest` stocks:
 *
 *   GET <data_url>/v2/stocks/bars?symbols=AAPL,MSFT&timeframe=1Min&start=...&end=...
 *   -> {"bars": {"AAPL": [{"t": "2026-10-28T13:30:00Z", "o":.., "h":.., "l":.., "c":.., "v":..}], ...},
 *       "next_page_token": "..."}
 *
 * Prices are split-adjusted and from the SIP feed (all US exchanges) unless config/general.json's "alpaca"
 * section says otherwise ("history_feed", "history_adjustment"), as for AlpacaHistory. Retries when Alpaca's
 * rate limit is hit.
 */
class AlpacaMultiBars
{
    public:
        using BarsBySymbol = std::map<std::string, std::vector<IntradayBar>>;

        // client: how it talks to Alpaca (none: a real AlpacaClient; tests pass a fake)
        explicit AlpacaMultiBars(const json& config, std::shared_ptr<AlpacaClient> client = nullptr);

        // Bars between two UTC times (RFC3339) for every symbol, oldest first, times in New York time.
        // A symbol with no bars is left out. ok is false if any request failed.
        BarsBySymbol fetch(const std::vector<std::string>& symbols, const std::string& timeframe,
                           const std::string& startUtc, const std::string& endUtc, size_t symbolsPerRequest,
                           bool& ok);

        // Active, tradable US stocks and ETFs listed on the main exchanges (no OTC), with plain symbols
        // (letters only: Alpaca's data API spells classes like BRK.B differently). Sorted.
        std::vector<std::string> tradableSymbols(bool& ok);

        // One bar of a response, {"t": ..., "o": ..., "h": ..., "l": ..., "c": ..., "v": ...}
        static IntradayBar parseBar(const json& bar);

        // Is this one of the plain, listed symbols tradableSymbols keeps? (public for tests)
        static bool isListedStock(const json& asset);

    private:
        // One batch of symbols, every page
        bool fetchBatch(const std::vector<std::string>& symbols, const std::string& timeframe,
                        const std::string& startUtc, const std::string& endUtc, BarsBySymbol& out);

        std::shared_ptr<AlpacaClient> client;
        std::string dataUrl;
        std::string tradingUrl;
        std::string feed;
        std::string adjustment;
};
