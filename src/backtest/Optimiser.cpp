#include "Optimiser.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <random>
#include <thread>
#include "StrategyTester.hpp"
#include "../strategy_engine/MACD.hpp"
#include "../strategy_engine/MEANREV.hpp"
#include "../strategy_engine/RSI.hpp"
#include "../strategy_engine/SCALP.hpp"
#include "../util/MarketTime.hpp"

namespace
{
    // Defaults for settings missing from optimisation.json / screen.json
    const int DEFAULT_MIN_CLOSED_TRADES = 30;   // fewer trades than this and a backtest's Sharpe means little
    const size_t DEFAULT_TOP_N_TO_TEST = 10;    // the best settings on training that are run on the test period

    // "objective": what makes one setting better than another
    const std::string SHARPE = "sharpe";
    const std::string TOTAL_RETURN = "total_return";
    const std::string RETURN_OVER_DRAWDOWN = "return_over_drawdown";

    // max_combinations picks the same random sample every run
    const unsigned SAMPLE_SEED = 12345;
}

Optimiser::Optimiser(const json& base, const json& optimisationSettings)
: baseConfig(base),
  settings(optimisationSettings)
{
}

json
Optimiser::currentSettings(const std::string& strategyName) const
{
    for (const auto& strategy : baseConfig.value("strategies", json::array()))
    {
        if (strategy.value("name", "") == strategyName)
        {
            json copy = strategy;
            copy["active"] = 1;
            return copy;
        }
    }
    return {{"name", strategyName}, {"active", 1}};
}

bool
Optimiser::isValid(const json& s)
{
    std::string name = s.value("name", "");
    if (name == MACD::NAME)
        return s.value("short_period", 0) < s.value("long_period", 0);
    if (name == RSI::NAME)
        return s.value("oversold_threshold", 0.0) < s.value("overbought_threshold", 0.0);
    if (name == MEANREV::NAME || name == SCALP::NAME)
        return s.value("exit_threshold", 0.0) < s.value("entry_threshold", 0.0);
    return true;
}

std::vector<json>
Optimiser::parameterGrid(const std::string& strategyName) const
{
    json ranges = settings.at("strategies").at(strategyName);

    // Cartesian product of every listed value
    std::vector<json> grid = {currentSettings(strategyName)};
    for (const auto& [parameter, values] : ranges.items())
    {
        std::vector<json> expanded;
        for (const auto& partial : grid)
        {
            for (const auto& value : values)
            {
                json next = partial;
                next[parameter] = value;
                expanded.push_back(next);
            }
        }
        grid.swap(expanded);
    }

    grid.erase(std::remove_if(grid.begin(), grid.end(), [](const json& s) { return !isValid(s); }), grid.end());

    // Optionally a fixed random sample, so big searches can be cut down (same sample every run)
    size_t maxCombinations = settings.value("max_combinations", 0);
    if (maxCombinations > 0 && grid.size() > maxCombinations)
    {
        std::mt19937 random(SAMPLE_SEED);
        std::shuffle(grid.begin(), grid.end(), random);
        grid.resize(maxCombinations);
    }
    return grid;
}

json
Optimiser::searchedParameters(const json& strategySettings) const
{
    json parameters = json::object();
    std::string name = strategySettings.value("name", "");
    if (!settings.at("strategies").contains(name))
        return parameters;
    for (const auto& [parameter, values] : settings["strategies"][name].items())
        if (strategySettings.contains(parameter))
            parameters[parameter] = strategySettings[parameter];
    return parameters;
}

PerformanceSummary
Optimiser::evaluate(const json& strategySettings, const std::vector<MarketCondition>& bars, size_t warmupBars) const
{
    json config = baseConfig;
    config["strategies"] = json::array({strategySettings});

    StrategyTester tester(config);
    tester.useCosts(TradingCosts(settings));
    tester.keepEquityCurve(false);
    tester.setWarmupBars(warmupBars);
    tester.runOn(bars);
    return tester.getSummary();
}

double
Optimiser::objective(const PerformanceSummary& s) const
{
    std::string goal = settings.value("objective", SHARPE);
    if (goal == TOTAL_RETURN)
        return s.roiPercent;
    if (goal == RETURN_OVER_DRAWDOWN)
        return s.maxDrawdownPercent > 0 ? s.roiPercent / s.maxDrawdownPercent : s.roiPercent;
    return s.sharpeRatio;
}

std::vector<Optimiser::Result>
Optimiser::search(const std::vector<json>& candidates,
                  const std::vector<MarketCondition>& train,
                  const std::vector<MarketCondition>& test,
                  int threads,
                  const std::function<void(size_t, size_t)>& onProgress,
                  size_t testWarmupBars) const
{
    std::vector<Result> results(candidates.size());
    int minTrades = settings.value("min_closed_trades", DEFAULT_MIN_CLOSED_TRADES);
    size_t topN = settings.value("top_n_to_test", DEFAULT_TOP_N_TO_TEST);
    threads = std::max(1, threads);

    // Each worker takes the next untried candidate until there are none left.
    // Workers only read the shared bars and write their own result slot, so no locking is needed there.
    auto runAll = [&](const std::vector<size_t>& indices, bool onTest) {
        std::atomic<size_t> next{0};
        std::atomic<size_t> done{0};
        std::mutex progressMutex;

        auto worker = [&]() {
            while (true)
            {
                size_t i = next.fetch_add(1);
                if (i >= indices.size())
                    return;
                Result& result = results[indices[i]];
                if (onTest)
                {
                    result.test = evaluate(result.settings, test, testWarmupBars);
                    result.tested = true;
                }
                else
                {
                    result.train = evaluate(result.settings, train);
                    result.eligible = result.train.closedTrades >= minTrades;
                    result.score = objective(result.train);
                }
                size_t finished = done.fetch_add(1) + 1;
                if (onProgress)
                {
                    std::lock_guard<std::mutex> lock(progressMutex);
                    onProgress(finished, indices.size());
                }
            }
        };

        std::vector<std::thread> pool;
        for (int t = 0; t < threads; t++)
            pool.emplace_back(worker);
        for (auto& thread : pool)
            thread.join();
    };

    // 1. Every candidate on the training data
    std::vector<size_t> all(candidates.size());
    for (size_t i = 0; i < candidates.size(); i++)
    {
        results[i].settings = candidates[i];
        all[i] = i;
    }
    runAll(all, false);

    // 2. Rank: eligible first, then by objective (ties broken by order tried, so results are repeatable)
    std::stable_sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
        if (a.eligible != b.eligible)
            return a.eligible;
        return a.score > b.score;
    });

    // 3. The top ones on the test data
    std::vector<size_t> top;
    for (size_t i = 0; i < results.size() && top.size() < topN; i++)
        if (results[i].eligible && test.size() > testWarmupBars)
            top.push_back(i);
    runAll(top, true);

    return results;
}

void
Optimiser::splitByMonths(const std::vector<MarketCondition>& bars, int testMonths,
                         std::vector<MarketCondition>& train, std::vector<MarketCondition>& test)
{
    train.clear();
    test.clear();
    if (bars.empty() || testMonths <= 0)
    {
        train = bars;
        return;
    }

    // Test period starts testMonths calendar months before the last bar's month ends
    std::string cutoff = MarketTime::monthsBefore(MarketTime::firstOfMonth(bars.back().DateTime), testMonths - 1);

    for (const auto& bar : bars)
        (bar.DateTime < cutoff ? train : test).push_back(bar);
}
