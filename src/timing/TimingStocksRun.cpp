#include "TimingStocksRun.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include "TimingBacktest.hpp"
#include "TimingRule.hpp"
#include "TimingSeries.hpp"
#include "TimingSettings.hpp"
#include "../morning/MorningData.hpp"
#include "../morning/MorningReport.hpp"
#include "../morning/ResearchProgress.hpp"
#include "../morning/StockSelection.hpp"
#include "../pairs/DailyPrices.hpp"
#include "../util/SaveData.hpp"

namespace fs = std::filesystem;

namespace
{
    const std::string FOLDER = "timing_stocks";
    const std::string REPORT_FILE = "report.json";
    const std::string TITLE = "MARKET TIMING SEARCH: MANY STOCKS";
    const std::string CHOSEN = "timed";
    const std::string HELD = "held";
    const std::string EVERY_STOCK_HELD = "every stock held";
    const std::string COUNT_LABEL = "symbols";
    const std::string PICKS_NAME = "each stock's best rule";
    const std::string BENCHMARK_SYMBOL = "SPY";

    const std::string STAGE_UNIVERSE = "Picking the stocks";
    const std::string STAGE_DATA = "Daily bars";
    const std::string STAGE_SEARCH = "Trading every rule on every stock";
    const std::string STAGE_FINALISTS = "Finalists and picks against random timings";
    const std::string STAGE_REPORT = "Writing the report";
    const int REPORT_INDENT = 1;
    const size_t PICK_ROWS = 300;
    const double SIGNIFICANT = 0.95;   // beating this share of random timings
    const double PERCENT = 100.0;

    using Measures = TimingBacktest::Measures;

    // One rule on one stock, what the search keeps
    struct Result
    {
        float edge[3] = {0, 0, 0};       // by period: development, validation, holdout
        float cagr[3] = {0, 0, 0};
        float capture[3] = {0, 0, 0};
        float exposure[3] = {0, 0, 0};
        float sharpe[3] = {0, 0, 0};
        int trades = 0;                  // in development
        bool eligible = false;
    };

    struct Stock
    {
        std::string symbol;
        std::vector<Result> results;     // by rule
        Measures held[3];                // the stock bought and held, by period
        size_t pick = 0;                 // its best rule in development
        bool picked = false;
        size_t randomPick = 0;           // a rule picked at random from its eligible ones
        std::map<std::string, double> pickPercentile;
    };

    double median(std::vector<double> values)
    {
        if (values.empty())
            return 0;
        std::sort(values.begin(), values.end());
        size_t middle = values.size() / 2;
        return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
    }

    double share(size_t count, size_t of) { return of ? static_cast<double>(count) / of : 0; }

    json measuresJson(double cagr, double edge, double capture, double exposure, double sharpe)
    {
        return {{"cagr", cagr}, {"edge", edge}, {"capture", capture}, {"exposure", exposure}, {"sharpe", sharpe}};
    }

    // An equal-weighted portfolio's returns, built a stock at a time on a master calendar, with its trades and costs
    struct Portfolio
    {
        std::vector<double> sum, costs;
        std::vector<int> count, trades;

        explicit Portfolio(size_t days) : sum(days, 0), costs(days, 0), count(days, 0), trades(days, 0) {}

        void add(const TimingBacktest::Traded& traded, const std::map<std::string, size_t>& calendar)
        {
            for (size_t i = 0; i < traded.days.size(); i++)
            {
                auto found = calendar.find(traded.days[i]);
                if (found == calendar.end())
                    continue;
                sum[found->second] += traded.returns[i];
                count[found->second]++;
                if (!traded.changes.empty())
                {
                    trades[found->second] += traded.changes[i];
                    costs[found->second] += traded.costs[i];
                }
            }
        }

        MorningBacktest::Line line(const std::vector<std::string>& days, const std::string& variant,
                                   const std::string& selection, double capital) const
        {
            MorningBacktest::Line made;
            made.variant = variant;
            made.selection = selection;
            made.equity = capital;
            for (size_t day = 0; day < days.size(); day++)
            {
                if (count[day] == 0)
                    continue;
                double morning = made.equity;
                double change = sum[day] / count[day];
                made.days.push_back(days[day]);
                made.returns.push_back(change);
                made.equity *= 1 + change;
                if (trades[day] == 0)
                    continue;
                StockDay summary;   // the day's changes of exposure across the stocks, as one row
                summary.symbol = variant;
                summary.day = days[day];
                summary.trades = trades[day];
                summary.costs = costs[day] / count[day] * morning;
                summary.netPnl = made.equity - morning;
                summary.grossPnl = summary.netPnl + summary.costs;
                summary.wins = summary.netPnl > 0 ? 1 : 0;
                made.stockDays.push_back(summary);
            }
            return made;
        }
    };
}

void
TimingStocksRun::printUsage(std::ostream& out)
{
    out << "Usage: timing_stocks_app [--holdout] [--offline]\n\n"
        << "The timing search (config/timing_stocks.json) on hundreds of the most traded stocks and funds: each\n"
        << "stock's best rule, and the rules that work across them, picked on development and tested on validation.\n"
        << "Saves data/timing_stocks/report.json.\n\n"
        << "  --holdout   also trade the finalists and picks through the holdout. Do this once, at the end\n"
        << "  --offline   don't download: use only what's in data/pairs/ and data/timing_stocks/\n";
}

TimingStocksRun::TimingStocksRun(const json& algoConfig, const json& timingConfig, const std::string& dataRoot)
    : algoConfig(algoConfig), timingConfig(timingConfig), dataRoot(dataRoot),
      folder((fs::path(dataRoot) / FOLDER).string())
{
}

int
TimingStocksRun::run(const MorningRun::Options& options)
{
    TimingSettings settings = TimingSettings::fromConfig(timingConfig, ResearchProgress::yesterday());
    fs::create_directories(folder);
    ResearchProgress progress(folder);
    progress.save(ResearchProgress::RUNNING, STAGE_UNIVERSE, 0, 1, true);
    std::string holdoutOpened = MorningHoldout::openedAt(folder);
    std::vector<std::string> periods = {MorningSettings::DEVELOPMENT, MorningSettings::VALIDATION};
    if (options.holdout)
        periods.push_back(MorningSettings::HOLDOUT);
    const size_t periodCount = periods.size();

    // 1. The symbols: the most traded over the shared daily bars, and the funds always included
    std::vector<std::string> symbols = settings.alwaysSymbols;
    {
        DailyPrices::Request request{settings.rankingFrom, settings.end, settings.loadDollarVolume / 2,
                                     settings.loadDollarVolume, options.offline, settings.alwaysSymbols};
        PriceMatrix prices;
        size_t ignored = 0;
        if (!DailyPrices::load(algoConfig, dataRoot, request, progress, prices, ignored))
        {
            progress.save(ResearchProgress::FAILED, "", 0, 0, true);
            return 1;
        }
        std::set<std::string> skip(settings.notStocks.begin(), settings.notStocks.end());
        skip.insert(settings.alwaysSymbols.begin(), settings.alwaysSymbols.end());
        std::vector<std::pair<double, std::string>> ranked;
        for (size_t stock = 0; stock < prices.stocks(); stock++)
        {
            if (skip.count(prices.symbols[stock]))
                continue;
            double dollars = 0;
            for (double value : prices.dollarVolume[stock])
                dollars += value;
            ranked.push_back({-dollars, prices.symbols[stock]});
        }
        std::sort(ranked.begin(), ranked.end());
        for (size_t i = 0; i < ranked.size() && i < settings.universeStocks; i++)
            symbols.push_back(ranked[i].second);
    }

    // 2. Their daily bars from data_start, dividends in, and the cash fund's
    json dataConfig = algoConfig;
    dataConfig["alpaca"]["history_adjustment"] = settings.adjustment;
    MorningData data(dataConfig, folder, options.offline);
    std::vector<std::string> downloads = symbols;
    if (!settings.cashSymbol.empty())
        downloads.push_back(settings.cashSymbol);
    if (!data.ensureDailyBars(downloads, settings.dataStart, settings.end, [&](size_t done, size_t total) {
            progress.stage(STAGE_DATA, done, total);
        }))
        std::cerr << "WARNING - Some daily bars couldn't be downloaded; those symbols are left out" << std::endl;
    std::vector<quant::DailyBar> cash = settings.cashSymbol.empty() ? std::vector<quant::DailyBar>()
                                                                    : data.dailyBars(settings.cashSymbol);
    auto barsOf = [&](const std::string& symbol) {
        std::vector<quant::DailyBar> bars;
        for (const auto& bar : data.dailyBars(symbol))
            if (bar.date >= settings.dataStart && bar.date <= settings.end)
                bars.push_back(bar);
        return bars;
    };
    // Kept: those trading from the first month of data_start (a full history for every period)
    std::string firstMonth = settings.dataStart.substr(0, 7);
    std::vector<std::string> kept;
    for (const auto& symbol : symbols)
    {
        auto bars = barsOf(symbol);
        if (bars.size() > 1 && bars.front().date.substr(0, 7) == firstMonth && bars.back().date >= settings.validationEnd)
            kept.push_back(symbol);
    }
    std::cout << kept.size() << " of " << symbols.size() << " symbols have bars from " << firstMonth << std::endl;
    if (kept.empty())
    {
        std::cerr << "ERROR - No symbol has daily bars from " << firstMonth << std::endl;
        progress.save(ResearchProgress::FAILED, "", 0, 0, true);
        return 1;
    }

    // 3. Every rule on every stock, keeping only its numbers
    std::vector<TimingRule> rules = TimingRule::expand(settings.families);
    MorningSettings reportSettings = settings.forReport();
    std::string lastDate = options.holdout ? settings.end : settings.validationEnd;
    std::vector<Stock> stocks;
    auto window = [&](const TimingSeries& series, size_t& first, size_t& last) {
        first = static_cast<size_t>(std::lower_bound(series.days.begin(), series.days.end(), settings.start) - series.days.begin());
        last = static_cast<size_t>(std::upper_bound(series.days.begin(), series.days.end(), lastDate) - series.days.begin());
        last = last == 0 ? 0 : last - 1;
    };
    for (size_t s = 0; s < kept.size(); s++)
    {
        progress.stage(STAGE_SEARCH, s, kept.size());
        TimingSeries series(barsOf(kept[s]), cash);
        TimingBacktest backtest(settings, series);
        size_t first = 0, last = 0;
        window(series, first, last);
        Stock stock;
        stock.symbol = kept[s];
        TimingBacktest::Traded held = backtest.buyAndHold(first, last);
        for (size_t p = 0; p < periodCount; p++)
            stock.held[p] = backtest.measure(held, periods[p]);
        for (const auto& rule : rules)
        {
            TimingBacktest::Traded traded = backtest.trade(rule.targets(series), first, last);
            Result result;
            for (size_t p = 0; p < periodCount; p++)
            {
                Measures m = backtest.measure(traded, periods[p]);
                result.edge[p] = static_cast<float>(m.edge);
                result.cagr[p] = static_cast<float>(m.cagr);
                result.capture[p] = static_cast<float>(m.capture);
                result.exposure[p] = static_cast<float>(m.exposure);
                result.sharpe[p] = static_cast<float>(m.sharpe);
                if (p == 0)
                    result.trades = m.trades;
            }
            result.eligible = result.exposure[0] >= settings.minExposure && result.exposure[0] <= settings.maxExposure
                           && result.trades >= settings.minTrades;
            stock.results.push_back(result);
        }
        // Its pick (the best edge in development), and a rule picked at random from the same eligible ones
        std::vector<size_t> eligible;
        for (size_t r = 0; r < rules.size(); r++)
            if (stock.results[r].eligible)
                eligible.push_back(r);
        if (!eligible.empty())
        {
            stock.picked = true;
            stock.pick = *std::max_element(eligible.begin(), eligible.end(), [&](size_t a, size_t b) {
                return stock.results[a].edge[0] < stock.results[b].edge[0];
            });
            std::mt19937_64 random(StockSelection::stableHash(stock.symbol) ^ settings.randomSeed);
            stock.randomPick = eligible[std::uniform_int_distribution<size_t>(0, eligible.size() - 1)(random)];
        }
        stocks.push_back(std::move(stock));
    }

    // 4. Rules that travel: each rule's median development edge over the stocks it's eligible on
    struct Pooled
    {
        size_t rule = 0;
        double medianEdge[3] = {0, 0, 0}, medianCagr[3] = {0, 0, 0}, medianCapture[3] = {0, 0, 0};
        double medianExposure[3] = {0, 0, 0}, medianSharpe[3] = {0, 0, 0}, ahead[3] = {0, 0, 0};
        size_t eligibleOn = 0;
        bool finalist = false;
    };
    std::vector<Pooled> pooled;
    for (size_t r = 0; r < rules.size(); r++)
    {
        Pooled entry;
        entry.rule = r;
        for (size_t p = 0; p < periodCount; p++)
        {
            std::vector<double> edges, cagrs, captures, exposures, sharpes;
            size_t aheadCount = 0;
            for (const auto& stock : stocks)
                if (stock.results[r].eligible)
                {
                    const Result& result = stock.results[r];
                    edges.push_back(result.edge[p]);
                    cagrs.push_back(result.cagr[p]);
                    captures.push_back(result.capture[p]);
                    exposures.push_back(result.exposure[p]);
                    sharpes.push_back(result.sharpe[p]);
                    aheadCount += result.edge[p] > 0 ? 1 : 0;
                }
            entry.eligibleOn = edges.size();
            entry.medianEdge[p] = median(edges);
            entry.medianCagr[p] = median(cagrs);
            entry.medianCapture[p] = median(captures);
            entry.medianExposure[p] = median(exposures);
            entry.medianSharpe[p] = median(sharpes);
            entry.ahead[p] = share(aheadCount, edges.size());
        }
        if (entry.eligibleOn * 2 >= stocks.size())   // eligible on at least half of them
            pooled.push_back(entry);
    }
    std::stable_sort(pooled.begin(), pooled.end(), [](const Pooled& a, const Pooled& b) { return a.medianEdge[0] > b.medianEdge[0]; });
    std::vector<size_t> finalists;   // indexes into pooled
    std::map<std::string, size_t> perFamily;
    for (size_t i = 0; i < pooled.size() && finalists.size() < settings.finalists; i++)
        if (perFamily[rules[pooled[i].rule].family]++ < settings.perFamily)
        {
            pooled[i].finalist = true;
            finalists.push_back(i);
        }
    if (options.holdout)
    {
        std::vector<std::string> names = {PICKS_NAME};
        for (size_t i : finalists)
            names.push_back(rules[pooled[i].rule].name());
        holdoutOpened = MorningHoldout::open(folder, names);
    }

    // 5. The finalists on every stock as portfolios, and each stock's pick against random timings of itself
    std::vector<std::string> calendarDays;
    std::map<std::string, size_t> calendar;
    for (const auto& bar : barsOf(std::find(kept.begin(), kept.end(), BENCHMARK_SYMBOL) != kept.end() ? BENCHMARK_SYMBOL : kept.front()))
        if (bar.date >= settings.start && bar.date <= lastDate)
        {
            calendar[bar.date] = calendarDays.size();
            calendarDays.push_back(bar.date);
        }
    std::vector<Portfolio> timed(finalists.size(), Portfolio(calendarDays.size()));
    std::vector<Portfolio> randomly(finalists.size(), Portfolio(calendarDays.size()));
    Portfolio everyHeld(calendarDays.size()), picksPortfolio(calendarDays.size());
    for (size_t s = 0; s < stocks.size(); s++)
    {
        progress.stage(STAGE_FINALISTS, s, stocks.size());
        Stock& stock = stocks[s];
        TimingSeries series(barsOf(stock.symbol), cash);
        TimingBacktest backtest(settings, series);
        size_t first = 0, last = 0;
        window(series, first, last);
        everyHeld.add(backtest.buyAndHold(first, last), calendar);
        std::mt19937_64 random(StockSelection::stableHash(stock.symbol + TITLE) ^ settings.randomSeed);
        for (size_t f = 0; f < finalists.size(); f++)
        {
            if (!stock.results[pooled[finalists[f]].rule].eligible)
                continue;
            std::vector<double> targets = rules[pooled[finalists[f]].rule].targets(series);
            timed[f].add(backtest.trade(targets, first, last), calendar);
            randomly[f].add(backtest.trade(backtest.shuffled(targets, first, last, random), first, last), calendar);
        }
        if (!stock.picked)
            continue;
        std::vector<double> targets = rules[stock.pick].targets(series);
        picksPortfolio.add(backtest.trade(targets, first, last), calendar);
        std::map<std::string, int> beaten;
        for (int draw = 0; draw < settings.randomDraws; draw++)
        {
            TimingBacktest::Traded shuffled = backtest.trade(backtest.shuffled(targets, first, last, random), first, last);
            for (size_t p = 0; p < periodCount; p++)
                beaten[periods[p]] += stock.results[stock.pick].cagr[p] > backtest.measure(shuffled, periods[p]).cagr ? 1 : 0;
        }
        for (const auto& period : periods)
            stock.pickPercentile[period] = share(beaten[period], settings.randomDraws);
    }

    MorningBacktest::Results results;
    results.title = TITLE;
    results.chosen = CHOSEN;
    results.countLabel = COUNT_LABEL;
    results.symbols = static_cast<int>(stocks.size());
    results.daysTraded = static_cast<int>(calendarDays.size());
    results.holdoutTraded = options.holdout;
    for (size_t f = 0; f < finalists.size(); f++)
    {
        std::string name = rules[pooled[finalists[f]].rule].name();
        results.lines.push_back(timed[f].line(calendarDays, name, CHOSEN, settings.startingCapital));
        results.lines.push_back(randomly[f].line(calendarDays, name, MorningBacktest::RANDOM, settings.startingCapital));
        MorningSettings::Variant variant;
        variant.name = name;
        variant.step = rules[pooled[finalists[f]].rule].family;
        reportSettings.variants.push_back(variant);
    }
    results.lines.push_back(picksPortfolio.line(calendarDays, PICKS_NAME, CHOSEN, settings.startingCapital));
    results.lines.push_back(everyHeld.line(calendarDays, EVERY_STOCK_HELD, HELD, settings.startingCapital));
    {
        TimingSeries spy(barsOf(BENCHMARK_SYMBOL), cash);
        TimingBacktest backtest(settings, spy);
        size_t first = 0, last = 0;
        window(spy, first, last);
        MorningBacktest::Line benchmark = backtest.line(backtest.buyAndHold(first, last), MorningBacktest::BENCHMARK, BENCHMARK_SYMBOL);
        results.lines.push_back(benchmark);
    }
    for (const auto& entry : pooled)
        results.trialSharpes[MorningSettings::DEVELOPMENT].push_back(entry.medianSharpe[0] / std::sqrt(252.0));

    // 6. The leaderboard
    progress.save(ResearchProgress::RUNNING, STAGE_REPORT, 0, 1, true);
    json board = {{"rules_tried", rules.size() * stocks.size()}, {"symbol", std::to_string(stocks.size()) + " stocks"},
                  {"cash", settings.cashSymbol}, {"rules", rules.size()}, {"stocks", stocks.size()}};
    size_t eligiblePairs = 0;
    std::vector<size_t> aheadPairs(periodCount, 0);
    std::vector<double> developmentEdges, validationEdges;
    for (const auto& stock : stocks)
        for (const auto& result : stock.results)
            if (result.eligible)
            {
                eligiblePairs++;
                for (size_t p = 0; p < periodCount; p++)
                    aheadPairs[p] += result.edge[p] > 0 ? 1 : 0;
                developmentEdges.push_back(result.edge[0]);
                validationEdges.push_back(result.edge[1]);
            }
    board["eligible"] = eligiblePairs;
    for (size_t p = 0; p < periodCount; p++)
        board["ahead_of_mix"][periods[p]] = share(aheadPairs[p], eligiblePairs);
    board["edge_rank_correlation"] = TimingBacktest::spearman(developmentEdges, validationEdges);

    json families = json::array();
    for (const auto& family : TimingRule::familyNames())
    {
        std::vector<double> development, validation;
        size_t tried = 0;
        for (const auto& stock : stocks)
            for (size_t r = 0; r < rules.size(); r++)
                if (rules[r].family == family)
                {
                    tried++;
                    if (!stock.results[r].eligible)
                        continue;
                    development.push_back(stock.results[r].edge[0]);
                    validation.push_back(stock.results[r].edge[1]);
                }
        if (tried == 0)
            continue;
        size_t validationAhead = std::count_if(validation.begin(), validation.end(), [](double edge) { return edge > 0; });
        families.push_back({{"family", family}, {"tried", tried}, {"eligible", development.size()},
                            {"best_development_edge", development.empty() ? 0 : *std::max_element(development.begin(), development.end())},
                            {"median_development_edge", median(development)}, {"median_validation_edge", median(validation)},
                            {"validation_ahead", share(validationAhead, validation.size())}});
    }
    board["families"] = families;

    json top = json::array();
    for (size_t i = 0; i < pooled.size() && i < settings.leaderboardSize; i++)
    {
        const Pooled& entry = pooled[i];
        json row = {{"rank", i + 1}, {"rule", rules[entry.rule].name()}, {"family", rules[entry.rule].family},
                    {"finalist", entry.finalist}, {"eligible_on", entry.eligibleOn}};
        for (size_t p = 0; p < periodCount; p++)
            if (p == 0 || entry.finalist)
            {
                row[periods[p]] = measuresJson(entry.medianCagr[p], entry.medianEdge[p], entry.medianCapture[p],
                                               entry.medianExposure[p], entry.medianSharpe[p]);
                row["stocks_ahead"][periods[p]] = entry.ahead[p];
            }
        top.push_back(row);
    }
    board["top"] = top;

    // Each stock's pick: how often it beat holding as much without timing, holding the stock, and random timings
    json picks = json::array();
    std::vector<size_t> order;
    for (size_t s = 0; s < stocks.size(); s++)
        if (stocks[s].picked)
            order.push_back(s);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return stocks[a].results[stocks[a].pick].edge[0] > stocks[b].results[stocks[b].pick].edge[0];
    });
    json summary = json::object();
    for (size_t p = 0; p < periodCount; p++)
    {
        size_t aheadOfMix = 0, beatHold = 0, beatHoldSharpe = 0, significant = 0, randomAhead = 0;
        std::vector<double> edges;
        for (size_t s : order)
        {
            const Result& result = stocks[s].results[stocks[s].pick];
            aheadOfMix += result.edge[p] > 0 ? 1 : 0;
            beatHold += result.cagr[p] > stocks[s].held[p].cagr ? 1 : 0;
            beatHoldSharpe += result.sharpe[p] > stocks[s].held[p].sharpe ? 1 : 0;
            significant += stocks[s].pickPercentile[periods[p]] >= SIGNIFICANT ? 1 : 0;
            randomAhead += stocks[s].results[stocks[s].randomPick].edge[p] > 0 ? 1 : 0;
            edges.push_back(result.edge[p]);
        }
        summary[periods[p]] = {{"ahead_of_mix", share(aheadOfMix, order.size())}, {"beat_hold", share(beatHold, order.size())},
                               {"beat_hold_sharpe", share(beatHoldSharpe, order.size())},
                               {"beat_95_percent_of_random", share(significant, order.size())},
                               {"random_pick_ahead_of_mix", share(randomAhead, order.size())},
                               {"median_edge", median(edges)}};
    }
    for (size_t i = 0; i < order.size() && i < PICK_ROWS; i++)
    {
        const Stock& stock = stocks[order[i]];
        const Result& result = stock.results[stock.pick];
        json row = {{"symbol", stock.symbol}, {"rule", rules[stock.pick].name()}, {"family", rules[stock.pick].family}};
        for (size_t p = 0; p < periodCount; p++)
            row[periods[p]] = {{"cagr", result.cagr[p]}, {"edge", result.edge[p]}, {"exposure", result.exposure[p]},
                               {"sharpe", result.sharpe[p]}, {"held_cagr", stock.held[p].cagr},
                               {"held_sharpe", stock.held[p].sharpe}, {"random_percentile", stock.pickPercentile.at(periods[p])}};
        picks.push_back(row);
    }
    board["stock_picks"] = {{"summary", summary}, {"rows", picks}};

    MorningReport report(reportSettings, results);
    report.print(std::cout);
    json saved = report.toJson(holdoutOpened, data.downloads());
    saved["leaderboard"] = board;
    SaveData::write((fs::path(folder) / REPORT_FILE).string(), saved.dump(REPORT_INDENT));

    std::cout << std::fixed << std::setprecision(0) << "\n" << stocks.size() << " stocks x " << rules.size() << " rules, "
              << eligiblePairs << " eligible pairs\n";
    for (size_t p = 0; p < periodCount; p++)
    {
        const json& line = summary[periods[p]];
        std::cout << "EACH STOCK'S BEST RULE, " << periods[p] << ": ahead of the exposure-matched mix "
                  << line["ahead_of_mix"].get<double>() * PERCENT << "% (a random rule per stock: "
                  << line["random_pick_ahead_of_mix"].get<double>() * PERCENT << "%), beat holding the stock "
                  << line["beat_hold"].get<double>() * PERCENT << "% (Sharpe " << line["beat_hold_sharpe"].get<double>() * PERCENT
                  << "%), beat 95% of random timings " << line["beat_95_percent_of_random"].get<double>() * PERCENT << "%\n";
    }
    std::cout << std::setprecision(2);
    for (size_t f = 0; f < finalists.size() && f < 10; f++)
    {
        const Pooled& entry = pooled[finalists[f]];
        std::cout << "  " << std::left << std::setw(70) << rules[entry.rule].name() << std::right << " median edge dev "
                  << entry.medianEdge[0] * PERCENT << "%  val " << entry.medianEdge[1] * PERCENT << "%  ahead on "
                  << entry.ahead[1] * PERCENT << "% of stocks in validation\n";
    }
    std::cout << "Saved " << (fs::path(folder) / REPORT_FILE).string() << " (" << data.downloads() << " requests to Alpaca)" << std::endl;
    progress.save(ResearchProgress::FINISHED, STAGE_REPORT, 1, 1, true);
    return 0;
}
