#pragma once

#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../data_access/MarketCondition.hpp"
#include "../oms/PerformanceTracker.hpp"

using json = nlohmann::json;

/**
 * Optimiser
 *
 * Searches for the best settings for a strategy by backtesting every combination of the values listed
 * in config/optimisation.json, in parallel.
 *
 * To avoid fooling ourselves with settings that only fit the past by luck ("overfitting"), the data is
 * split in two:
 *   - training data (everything except the last test_months): every combination is run and ranked here
 *   - test data (the last test_months): only the top few are run here, as a check on data they were
 *     never tuned on. A setting whose test result collapses was probably just fitted to noise.
 *
 * The recommended setting is always the best on the training data; the test data is never used to pick.
 */
class Optimiser
{
    public:
        struct Result
        {
            json settings;                // the full strategy settings tried
            PerformanceSummary train;
            PerformanceSummary test;
            bool tested = false;          // has it been run on the test data?
            bool eligible = false;        // enough trades on the training data to be ranked
            double score = 0;             // objective on the training data
        };

        // baseConfig: one stock's config (Portfolio::forTicker): risk limits, and its current "strategies"
        // settings:   optimisation.json
        Optimiser(const json& baseConfig, const json& settings);

        // Every combination of the listed values for a strategy (invalid ones, e.g. MACD short >= long,
        // removed), starting from the stock's current settings for it. If max_combinations > 0
        // and there are more, a fixed random sample of that size is used.
        std::vector<json> parameterGrid(const std::string& strategyName) const;

        // The stock's current settings for the strategy (forced active), or just its name if the stock doesn't
        // use it, so the strategy's built-in settings fill in anything the grid doesn't search
        json currentSettings(const std::string& strategyName) const;

        // Backtest one strategy setting over bars and return its performance. The first warmupBars bars only
        // fill the strategy's history (see StrategyTester::setWarmupBars).
        PerformanceSummary evaluate(const json& strategySettings, const std::vector<MarketCondition>& bars,
                                    size_t warmupBars = 0) const;

        // Run every setting on the training bars using `threads` threads, then the top ones on the test bars.
        // Results come back best first. onProgress(done, total) is called as runs finish.
        // testWarmupBars: how many bars at the start of `test` are history only (e.g. the end of the training
        // period, so strategies start the test period warmed up).
        std::vector<Result> search(const std::vector<json>& candidates,
                                   const std::vector<MarketCondition>& train,
                                   const std::vector<MarketCondition>& test,
                                   int threads,
                                   const std::function<void(size_t, size_t)>& onProgress = nullptr,
                                   size_t testWarmupBars = 0) const;

        // The number we rank by (higher is better), from optimisation.json "objective"
        double objective(const PerformanceSummary& summary) const;

        // Split bars into training and test sets: the test set is the last `testMonths` months
        static void splitByMonths(const std::vector<MarketCondition>& bars, int testMonths,
                                  std::vector<MarketCondition>& train, std::vector<MarketCondition>& test);

        // Just the parameters that were searched over, e.g. {"period":14,"oversold_threshold":30,...}
        json searchedParameters(const json& strategySettings) const;

        const json& getSettings() const { return settings; }

    private:
        static bool isValid(const json& strategySettings);

        json baseConfig;
        json settings;
};
