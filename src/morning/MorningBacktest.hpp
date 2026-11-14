#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "MorningData.hpp"
#include "MorningSettings.hpp"
#include "MorningTrade.hpp"
#include "StockSelection.hpp"

using json = nlohmann::json;

/**
 * MorningBacktest
 *
 * Walks the trading days in order. Each day it works out the universe and every stock's relative volume from
 * what was known by 09:35, picks the stocks in play and a random control, and trades every variant on both,
 * each line with its own account (compounding day to day). It also measures how far the picked stocks
 * typically move in 10 and 30 minutes, for step 1 of the plan.
 *
 * Holdout days are downloaded (so the final check needs no downloads) but only traded when asked.
 */
class MorningBacktest
{
    public:
        static inline const std::string IN_PLAY = "in play";
        static inline const std::string RANDOM = "random";
        static inline const std::string BENCHMARK = "SPY buy and hold";
        static inline const std::string BENCHMARK_SYMBOL = "SPY";

        // One rule on one selection of stocks, day by day
        struct Line
        {
            std::string variant;
            std::string selection;
            std::vector<std::string> days;
            std::vector<double> returns;           // each day's P&L / the line's equity that morning
            std::vector<StockDay> stockDays;       // what it made on each stock it traded
            std::vector<MorningTrade> trades;      // breakouts only: each trade
            double equity = 0;
        };

        // How far a selection's stocks move: the median over its stock-days of each day's median move
        struct Moves
        {
            int stockDays = 0;
            double move10Percent = 0;      // |return| over 10 minutes
            double move30Percent = 0;      // |return| over 30 minutes
            double atrPercent = 0;         // 14-day ATR / the day's open
            double relativeVolume = 0;
        };

        struct Results
        {
            std::vector<Line> lines;                // every variant x {in play, random}, then the benchmark
            std::map<std::string, Moves> moves;     // by selection, development and validation days only
            int symbols = 0;                        // in the universe before filters
            int daysTraded = 0;
            bool holdoutTraded = false;

            // How the report names things, so other studies (pairs_app) can use it too
            std::string title = "MORNING SCREEN";
            std::string chosen = IN_PLAY;           // the selection being tested; the other is RANDOM
            std::string countLabel = "Stocks a day";
        };

        // progress(stage, done, total)
        using Progress = std::function<void(const std::string&, size_t, size_t)>;

        MorningBacktest(const json& config, const MorningSettings& settings);

        // False (with a message on stderr) if the data couldn't be loaded
        bool run(MorningData& data, bool includeHoldout, const Progress& progress, Results& results) const;

        // The median of |return| over `minutes`-minute windows from 09:35 (each window's start a multiple of
        // `minutes` after it). 0 if there are none.
        static double medianMovePercent(const std::vector<IntradayBar>& bars, int minutes, int fromMinute);

    private:
        json config;
        MorningSettings settings;
};
