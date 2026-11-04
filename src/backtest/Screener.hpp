#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "Optimiser.hpp"
#include "../data_access/MarketCondition.hpp"
#include "../oms/PerformanceTracker.hpp"
#include "../quant/DailyBars.hpp"

using json = nlohmann::json;

/**
 * Screener
 *
 * Which stock / strategy combinations look worth trading? For every stock in a list and every strategy
 * (RSI, MACD, MEANREV), on 1-minute bars (the live app's bars) or daily bars:
 *
 *   1. Training years (before test_start): backtest every setting in the strategy's grid and pick the one
 *      with the best Sharpe (needing at least min_closed_trades trades). This is the optimiser's search.
 *   2. Test years (from test_start): run only that pick, on data it was never tuned on, and compare it with
 *      simply buying and holding the stock over the same days.
 *
 * The test years never influence a choice, so they show what tuning-then-trading would really have done.
 * Strategies start the test years warmed up (the end of the training years is fed in as history first).
 *
 * Settings: src/config/screen.json (stock list, costs, and a "minute" and a "daily" section with the dates and
 * the grid for each strategy). The section in use is merged over the common settings before it gets here,
 * with "bars_per_year" set so Sharpe ratios are annualised for that bar size.
 */
class Screener
{
    public:
        struct Row
        {
            std::string symbol;
            std::string strategy;
            bool found = false;              // some setting traded enough on the training years to be ranked
            json parameters;                 // the searched parameters of the pick, e.g. {"period":21,...}
            int combinations = 0;
            int trainDays = 0, testDays = 0;
            std::string testStart, testEnd;

            PerformanceSummary train;        // the pick on the training years
            PerformanceSummary test;         // the same setting on the test years
            double testReturnOnMaxPosition = 0;   // test P&L / (max_position_size shares at the first test price)

            double buyHoldReturn = 0;        // buy and hold over the test days
            double buyHoldSharpe = 0;
            double buyHoldMaxDrawdown = 0;   // fraction, positive

            bool beatsBuyHold() const { return found && test.sharpeRatio > buyHoldSharpe; }
        };

        // One line per strategy, over all stocks
        struct StrategySummary
        {
            std::string strategy;
            int stocks = 0;
            int found = 0;                   // stocks where a setting could be picked
            int positiveTest = 0;            // ...and it made money (Sharpe > 0) on the test years
            int beatBuyHold = 0;             // ...and had a better Sharpe than buy and hold
            double medianTrainSharpe = 0;
            double medianTestSharpe = 0;
            double trainTestCorrelation = 0; // across stocks: does a better training Sharpe mean a better test one?
        };

        // baseConfig: config.json (risk limits, current strategy settings); settings: screen.json
        Screener(const json& baseConfig, const json& settings);

        // Daily bars in the form the backtester reads (one bar per day at the close)
        static std::vector<MarketCondition> toBars(const std::string& symbol, const std::vector<quant::DailyBar>& daily);

        // Screen one stock with every strategy in `strategies`, on bars of any size (oldest first; the size must
        // match "bars_per_year"). Rows come back in the same order.
        // onProgress(strategy, done, total) reports backtests finished for the current strategy.
        std::vector<Row> screenStock(const std::string& symbol, const std::vector<MarketCondition>& bars,
                                     const std::vector<std::string>& strategies, int threads,
                                     const std::function<void(const std::string&, size_t, size_t)>& onProgress = nullptr) const;

        // Same, from daily bars
        std::vector<Row> screenStock(const std::string& symbol, const std::vector<quant::DailyBar>& daily,
                                     const std::vector<std::string>& strategies, int threads,
                                     const std::function<void(const std::string&, size_t, size_t)>& onProgress = nullptr) const;

        // Trading days covered by bars (distinct dates)
        static int countDays(const std::vector<MarketCondition>& bars);

        static std::vector<StrategySummary> summarise(const std::vector<Row>& rows, const std::vector<std::string>& strategies);

        // The n combinations with the best training Sharpe (what you'd pick knowing only the past)
        static std::vector<Row> topByTraining(const std::vector<Row>& rows, size_t n);

        // Combinations that did well in both periods and beat buy and hold on the test years
        static std::vector<Row> heldUp(const std::vector<Row>& rows, double minTrainSharpe, double minTestSharpe,
                                       int minTestTrades);

        // The n to take forward: held up in both periods (Sharpe >= 0.5 in each, beat buy and hold, at least
        // minTestTrades test trades), ranked by the weaker of the two Sharpe ratios so both periods must be good,
        // and at most one strategy per stock. May return fewer than n.
        static std::vector<Row> topPicks(const std::vector<Row>& rows, size_t n, int minTestTrades = 5);

        // Each pick's value at the end of every day over its whole history (training and test) with its chosen
        // setting, as if max_position_size shares at the first price were the capital: value = that capital + P&L
        // so far. These act like daily prices, so MonteCarloRisk can measure the risk of trading the picks together.
        // Names are "STOCK STRATEGY". bars: each stock's bars (any size) by symbol.
        quant::PriceTable pickValues(const std::vector<Row>& picks,
                                     const std::map<std::string, std::vector<MarketCondition>>& bars) const;
        quant::PriceTable pickValues(const std::vector<Row>& picks,
                                     const std::map<std::string, std::vector<quant::DailyBar>>& daily) const;

        // The full strategy settings of a pick (its searched parameters on top of the rest), ready for config.json
        json strategySettings(const Row& pick) const;

        // Save / load results (so a long screen can be resumed)
        static bool writeCsv(const std::string& path, const std::vector<Row>& rows);
        static std::vector<Row> readCsv(const std::string& path);

        // "period 21, oversold_threshold 35, ..." from the searched parameters
        static std::string describe(const json& parameters);

        const json& getSettings() const { return settings; }
        Optimiser& getOptimiser() { return optimiser; }

    private:
        json baseConfig;
        json settings;
        Optimiser optimiser;
};
