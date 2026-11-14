#include "MorningBacktest.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include "DriftBacktest.hpp"
#include "OpeningRangeBreakout.hpp"
#include "ScalpDay.hpp"
#include "../risk/DayEnd.hpp"
#include "../util/MarketTime.hpp"

namespace
{
    const double PERCENT = 100.0;
    const int SHORT_MOVE_MINUTES = 10;
    const int LONG_MOVE_MINUTES = 30;

    // Daily bars start early enough for the 14-day filters on the first opening day; opening bars start a
    // month before the first traded day, so relative volume has its 14 days of history from the start
    const int DAILY_MONTHS_BEFORE = 3;
    const int OPENING_MONTHS_BEFORE = 1;

    const std::string STAGE_UNIVERSE = "Listing US stocks";
    const std::string STAGE_DAILY = "Daily bars";
    const std::string STAGE_DAYS = "Picking and trading each day";

    double median(std::vector<double> values)
    {
        if (values.empty())
            return 0;
        size_t middle = values.size() / 2;
        std::nth_element(values.begin(), values.begin() + middle, values.end());
        double upper = values[middle];
        if (values.size() % 2)
            return upper;
        double lower = *std::max_element(values.begin(), values.begin() + middle);
        return (lower + upper) / 2;
    }

    // A rule ready to trade: a breakout variant, or a live strategy through the backtester
    struct Rule
    {
        std::unique_ptr<OpeningRangeBreakout> breakout;
        std::unique_ptr<ScalpDay> strategy;
    };

    // What a selection's stock-days measured, gathered before the medians are taken
    struct MoveSamples
    {
        std::vector<double> move10, move30, atr, relativeVolume;
    };

    // from: the first minute after the opening range
    void measure(const StockSelection::Pick& pick, const std::vector<IntradayBar>& bars, int from, MoveSamples& samples)
    {
        double move10 = MorningBacktest::medianMovePercent(bars, SHORT_MOVE_MINUTES, from);
        if (move10 <= 0 || bars.empty() || bars.front().open <= 0)
            return;
        samples.move10.push_back(move10);
        samples.move30.push_back(MorningBacktest::medianMovePercent(bars, LONG_MOVE_MINUTES, from));
        samples.atr.push_back(pick.atr / bars.front().open * PERCENT);
        if (pick.relativeVolume > 0)
            samples.relativeVolume.push_back(pick.relativeVolume);
    }

    MorningBacktest::Moves summarise(const MoveSamples& samples)
    {
        MorningBacktest::Moves moves;
        moves.stockDays = static_cast<int>(samples.move10.size());
        moves.move10Percent = median(samples.move10);
        moves.move30Percent = median(samples.move30);
        moves.atrPercent = median(samples.atr);
        moves.relativeVolume = median(samples.relativeVolume);
        return moves;
    }

    std::vector<std::string> symbolsOf(const std::vector<StockSelection::Pick>& picks)
    {
        std::vector<std::string> symbols;
        for (const auto& pick : picks)
            symbols.push_back(pick.symbol);
        return symbols;
    }

    // Trade one rule on one day's picks, from the line's equity that morning
    void tradeDay(const Rule& rule, const std::vector<StockSelection::Pick>& picks,
                  const MorningData::BarsBySymbol& bars, const std::string& day, MorningBacktest::Line& line)
    {
        double morning = line.equity, pnl = 0;
        for (const auto& pick : picks)
        {
            auto stock = bars.find(pick.symbol);
            if (stock == bars.end() || stock->second.empty())
                continue;
            StockDay result;
            if (rule.breakout)
            {
                auto trade = rule.breakout->trade(pick, stock->second, morning);
                if (!trade)
                    continue;
                line.trades.push_back(*trade);
                result = StockDay::of(*trade);
            }
            else
            {
                result = rule.strategy->run(pick, stock->second, morning);
                if (result.trades == 0)
                    continue;
            }
            pnl += result.netPnl;
            line.stockDays.push_back(result);
        }
        line.days.push_back(day);
        line.returns.push_back(morning > 0 ? pnl / morning : 0);
        line.equity += pnl;
    }
}

MorningBacktest::MorningBacktest(const json& config, const MorningSettings& settings)
    : config(config), settings(settings)
{
}

double
MorningBacktest::medianMovePercent(const std::vector<IntradayBar>& bars, int minutes, int fromMinute)
{
    std::map<int, double> closeAt;
    for (const auto& bar : bars)
        closeAt[MarketTime::minuteOfDay(bar.time)] = bar.close;
    std::vector<double> moves;
    for (int start = fromMinute; start + minutes < MarketTime::CLOSE_MINUTE; start += minutes)
    {
        auto from = closeAt.find(start), to = closeAt.find(start + minutes);
        if (from != closeAt.end() && to != closeAt.end() && from->second > 0)
            moves.push_back(std::fabs(to->second / from->second - 1) * PERCENT);
    }
    return median(moves);
}

bool
MorningBacktest::run(MorningData& data, bool includeHoldout, const Progress& progress, Results& results) const
{
    // 1. The universe and its daily bars
    if (progress)
        progress(STAGE_UNIVERSE, 0, 1);
    std::vector<std::string> universe;
    if (!data.symbols(universe))
    {
        std::cerr << "ERROR - Couldn't get the list of US stocks from Alpaca" << std::endl;
        return false;
    }
    results.symbols = static_cast<int>(universe.size());
    std::string dailyFrom = MarketTime::monthsBefore(settings.start, DAILY_MONTHS_BEFORE);
    bool dailyOk = data.ensureDailyBars(universe, dailyFrom, settings.end, [&](size_t done, size_t total) {
        if (progress)
            progress(STAGE_DAILY, done, total);
    });
    if (!dailyOk)
        std::cerr << "WARNING - Some daily bars couldn't be downloaded; those stocks are left out" << std::endl;

    // 2. Each day's universe, judged on the days before it, and the trading calendar (SPY's days)
    std::string openingFrom = MarketTime::monthsBefore(settings.start, OPENING_MONTHS_BEFORE);
    StockSelection::CandidatesByDay candidates;
    for (const auto& symbol : universe)
        StockSelection::addCandidates(symbol, data.dailyBars(symbol), settings, openingFrom, candidates);
    std::vector<quant::DailyBar> benchmark = data.dailyBars(BENCHMARK_SYMBOL);
    if (benchmark.empty())
    {
        std::cerr << "ERROR - No daily bars for " << BENCHMARK_SYMBOL << ", the trading calendar" << std::endl;
        return false;
    }

    // 3. The lines: every variant on the stocks in play and on the random ones
    DayEnd dayEnd(config);
    json costSettings = config;
    costSettings["slippage_percent"] = settings.slippagePercent;
    costSettings["starting_capital"] = settings.startingCapital;
    TradingCosts costs(costSettings);
    std::vector<Rule> rules;
    for (const auto& variant : settings.variants)
    {
        Rule rule;   // a drift variant has neither: it's traded after the walk through the days, on daily bars
        if (variant.isStrategy())
            rule.strategy = std::make_unique<ScalpDay>(config, variant.strategy, settings, costs);
        else if (!variant.isDrift())
            rule.breakout = std::make_unique<OpeningRangeBreakout>(settings, variant, costs, dayEnd);
        rules.push_back(std::move(rule));
        for (const auto& selection : {IN_PLAY, RANDOM})
        {
            Line line;
            line.variant = variant.name;
            line.selection = selection;
            line.equity = settings.startingCapital;
            results.lines.push_back(line);
        }
    }

    // 4. Day by day
    std::vector<std::string> calendar;
    for (const auto& bar : benchmark)
        if (bar.date >= openingFrom && bar.date <= settings.end)
            calendar.push_back(bar.date);
    StockSelection::OpeningVolumes volumes(settings);
    std::map<std::string, MoveSamples> samples;
    DriftBacktest::PicksByDay inPlayByDay, randomByDay;
    std::vector<std::string> tradedDays;
    for (size_t index = 0; index < calendar.size(); index++)
    {
        const std::string& day = calendar[index];
        if (progress)
            progress(STAGE_DAYS, index, calendar.size());
        const auto& today = candidates[day];
        std::vector<std::string> symbols;
        for (const auto& candidate : today)
            symbols.push_back(candidate.symbol);
        std::map<std::string, IntradayBar> openings = data.openingBars(day, symbols, settings.openingMinutes);

        if (day >= settings.start)
        {
            auto inPlay = StockSelection::inPlay(today, openings, volumes, settings);
            auto random = StockSelection::randomControl(today, openings, volumes, settings, day);
            std::vector<std::string> wanted = symbolsOf(inPlay), randomSymbols = symbolsOf(random);
            wanted.insert(wanted.end(), randomSymbols.begin(), randomSymbols.end());
            std::sort(wanted.begin(), wanted.end());
            wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
            MorningData::BarsBySymbol bars = data.minuteBars(day, wanted);

            bool holdout = settings.periodOf(day) == MorningSettings::HOLDOUT;
            if (!holdout || includeHoldout)
            {
                for (size_t r = 0; r < rules.size(); r++)
                {
                    if (!rules[r].breakout && !rules[r].strategy)
                        continue;
                    tradeDay(rules[r], inPlay, bars, day, results.lines[2 * r]);
                    tradeDay(rules[r], random, bars, day, results.lines[2 * r + 1]);
                }
                inPlayByDay[day] = inPlay;
                randomByDay[day] = random;
                tradedDays.push_back(day);
                results.daysTraded++;
                results.holdoutTraded = results.holdoutTraded || holdout;
            }
            if (!holdout)   // step 1 looks at development and validation days only
            {
                int afterRange = MarketTime::OPEN_MINUTE + settings.openingMinutes;
                for (const auto& pick : inPlay)
                    if (bars.count(pick.symbol))
                        measure(pick, bars.at(pick.symbol), afterRange, samples[IN_PLAY]);
                for (const auto& pick : random)
                    if (bars.count(pick.symbol))
                        measure(pick, bars.at(pick.symbol), afterRange, samples[RANDOM]);
            }
        }
        volumes.add(openings);
    }
    if (progress)
        progress(STAGE_DAYS, calendar.size(), calendar.size());

    // 5. Drift: the same picks held for days, on their daily bars
    DriftBacktest::DailyBySymbol daily;
    for (const auto* byDay : {&inPlayByDay, &randomByDay})
        for (const auto& [day, picks] : *byDay)
            for (const auto& pick : picks)
                if (!daily.count(pick.symbol))
                    for (const auto& bar : data.dailyBars(pick.symbol))
                        daily[pick.symbol][bar.date] = bar;
    DriftBacktest drift(settings, costs);
    for (size_t r = 0; r < settings.variants.size(); r++)
        if (settings.variants[r].isDrift())
        {
            drift.run(settings.variants[r], inPlayByDay, daily, tradedDays, results.lines[2 * r]);
            drift.run(settings.variants[r], randomByDay, daily, tradedDays, results.lines[2 * r + 1]);
        }

    // 6. The benchmark, on the days the lines traded
    Line spy;
    spy.variant = BENCHMARK;
    spy.selection = BENCHMARK_SYMBOL;
    spy.equity = settings.startingCapital;
    std::set<std::string> traded(results.lines.empty() ? std::set<std::string>()
                                 : std::set<std::string>(results.lines[0].days.begin(), results.lines[0].days.end()));
    for (size_t i = 1; i < benchmark.size(); i++)
        if (traded.count(benchmark[i].date) && benchmark[i - 1].close > 0)
        {
            double change = benchmark[i].close / benchmark[i - 1].close - 1;
            spy.days.push_back(benchmark[i].date);
            spy.returns.push_back(change);
            spy.equity *= 1 + change;
        }
    results.lines.push_back(spy);

    for (const auto& [selection, gathered] : samples)
        results.moves[selection] = summarise(gathered);
    return true;
}
