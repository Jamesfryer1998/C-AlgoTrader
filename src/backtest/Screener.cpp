#include "Screener.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include "StrategyTester.hpp"
#include <sstream>
#include "../quant/Stats.hpp"
#include "../util/MarketTime.hpp"
#include "../util/SaveData.hpp"

namespace
{
    // Defaults for settings missing from screen.json
    const std::string DEFAULT_TEST_START = "2024-01-01";
    const int DEFAULT_MIN_TRAIN_DAYS = 500;
    const int DEFAULT_MIN_TEST_DAYS = 120;
    // Bars of training data fed in before the test years, so strategies start them warmed up
    const size_t DEFAULT_TEST_WARMUP_BARS = 320;

    // Daily bars are stamped at the close
    const std::string DAILY_BAR_TIME = " 16:00:00";

    // A pick's value never drops below this share of its starting capital (keeps the returns finite)
    const double LOWEST_VALUE_FRACTION = 0.01;
}

// Sharpe ratios annualised for the bar size being screened (from the settings; daily if not given)
static json withBarsPerYear(json config, const json& settings)
{
    config["bars_per_year"] = settings.value("bars_per_year", quant::TRADING_DAYS);
    return config;
}

static json pickOnlyTheBest(json settings)
{
    settings["top_n_to_test"] = 1;   // only the training pick is ever run on the test years
    return settings;
}

Screener::Screener(const json& baseConfig, const json& settings)
    : baseConfig(withBarsPerYear(baseConfig, settings)),
      settings(settings),
      maxPosition(baseConfig.at("max_position_size").get<double>()),
      optimiser(withBarsPerYear(baseConfig, settings), pickOnlyTheBest(settings))
{
}

std::vector<MarketCondition>
Screener::toBars(const std::string& symbol, const std::vector<quant::DailyBar>& daily)
{
    std::vector<MarketCondition> bars;
    bars.reserve(daily.size());
    for (const auto& d : daily)
    {
        // Volume isn't used by the strategies; clamp it so huge split-adjusted volumes fit
        int volume = static_cast<int>(std::min(d.volume, static_cast<double>(INT_MAX)));
        bars.emplace_back(d.date + DAILY_BAR_TIME, symbol, static_cast<float>(d.open), static_cast<float>(d.close),
                          volume, "1d");
    }
    return bars;
}

int
Screener::countDays(const std::vector<MarketCondition>& bars)
{
    int days = 0;
    std::string last;
    for (const auto& bar : bars)
    {
        std::string day = MarketTime::datePart(bar.DateTime);
        if (day != last)
        {
            days++;
            last = day;
        }
    }
    return days;
}

std::vector<Screener::Row>
Screener::screenStock(const std::string& symbol, const std::vector<quant::DailyBar>& daily,
                      const std::vector<std::string>& strategies, int threads,
                      const std::function<void(const std::string&, size_t, size_t)>& onProgress) const
{
    return screenStock(symbol, toBars(symbol, daily), strategies, threads, onProgress);
}

std::vector<Screener::Row>
Screener::screenStock(const std::string& symbol, const std::vector<MarketCondition>& all,
                      const std::vector<std::string>& strategies, int threads,
                      const std::function<void(const std::string&, size_t, size_t)>& onProgress) const
{
    std::string testStart = settings.value("test_start", DEFAULT_TEST_START);
    int minTrainDays = settings.value("min_train_days", DEFAULT_MIN_TRAIN_DAYS);
    int minTestDays = settings.value("min_test_days", DEFAULT_MIN_TEST_DAYS);
    double barsPerYear = baseConfig.value("bars_per_year", quant::TRADING_DAYS);
    size_t warmupWanted = settings.value("test_warmup_bars", DEFAULT_TEST_WARMUP_BARS);

    // 1. Split: training years before test_start, test years from it. The test run is given the last
    //    training bars first as history only, so strategies start the test years warmed up.
    std::vector<MarketCondition> train, test;
    for (const auto& bar : all)
        (MarketTime::datePart(bar.DateTime) < testStart ? train : test).push_back(bar);

    std::vector<Row> rows;
    for (const auto& name : strategies)
    {
        Row row;
        row.symbol = symbol;
        row.strategy = name;
        row.trainDays = countDays(train);
        row.testDays = countDays(test);
        if (!test.empty())
        {
            row.testStart = MarketTime::datePart(test.front().DateTime);
            row.testEnd = MarketTime::datePart(test.back().DateTime);
        }
        rows.push_back(row);
    }
    if (rows.empty() || rows.front().trainDays < minTrainDays || rows.front().testDays < minTestDays)
        return rows;   // not enough history (e.g. listed recently): nothing found

    // 2. Buy and hold over the test days, the benchmark every strategy has to beat
    quant::Vector closes;
    for (const auto& bar : test)
        closes.push_back(bar.Close);
    quant::Performance buyHold = quant::performance(quant::simpleReturns(closes), barsPerYear);
    for (auto& row : rows)
    {
        row.buyHoldReturn = buyHold.totalReturn;
        row.buyHoldSharpe = buyHold.sharpe;
        row.buyHoldMaxDrawdown = buyHold.maxDrawdown;
    }

    size_t warmup = std::min(warmupWanted, train.size());
    std::vector<MarketCondition> testWithHistory(train.end() - warmup, train.end());
    testWithHistory.insert(testWithHistory.end(), test.begin(), test.end());

    // 3. Each strategy: tune on the training years, run the pick on the test years
    for (auto& row : rows)
    {
        std::vector<json> grid = optimiser.parameterGrid(row.strategy);
        row.combinations = static_cast<int>(grid.size());

        auto progress = [&](size_t done, size_t total) {
            if (onProgress)
                onProgress(row.strategy, done, total);
        };
        std::vector<Optimiser::Result> results = optimiser.search(grid, train, testWithHistory, threads, progress, warmup);
        if (results.empty() || !results.front().eligible || !results.front().tested)
            continue;

        const Optimiser::Result& pick = results.front();
        row.found = true;
        row.parameters = optimiser.searchedParameters(pick.settings);
        row.train = pick.train;
        row.test = pick.test;
        double fullPosition = maxPosition * test.front().Close;
        row.testReturnOnMaxPosition = fullPosition > 0 ? pick.test.totalPnL / fullPosition : 0.0;
    }
    return rows;
}

static double median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    size_t mid = values.size() / 2;
    return values.size() % 2 ? values[mid] : (values[mid - 1] + values[mid]) / 2.0;
}

std::vector<Screener::StrategySummary>
Screener::summarise(const std::vector<Row>& rows, const std::vector<std::string>& strategies)
{
    std::vector<StrategySummary> out;
    for (const auto& name : strategies)
    {
        StrategySummary s;
        s.strategy = name;
        std::vector<double> trainSharpe, testSharpe;
        for (const auto& row : rows)
        {
            if (row.strategy != name)
                continue;
            s.stocks++;
            if (!row.found)
                continue;
            s.found++;
            if (row.test.sharpeRatio > 0) s.positiveTest++;
            if (row.beatsBuyHold()) s.beatBuyHold++;
            trainSharpe.push_back(row.train.sharpeRatio);
            testSharpe.push_back(row.test.sharpeRatio);
        }
        s.medianTrainSharpe = median(trainSharpe);
        s.medianTestSharpe = median(testSharpe);
        s.trainTestCorrelation = trainSharpe.size() >= 3 ? quant::correlation(trainSharpe, testSharpe) : 0.0;
        out.push_back(s);
    }
    return out;
}

std::vector<Screener::Row>
Screener::bestOnTraining(const std::vector<Row>& rows, size_t n)
{
    std::vector<Row> found;
    for (const auto& row : rows)
        if (row.found)
            found.push_back(row);
    std::stable_sort(found.begin(), found.end(),
                     [](const Row& a, const Row& b) { return a.train.sharpeRatio > b.train.sharpeRatio; });
    if (found.size() > n)
        found.resize(n);
    return found;
}

std::vector<Screener::Row>
Screener::goodInBothPeriods(const std::vector<Row>& rows, int minTestTrades)
{
    std::vector<Row> out;
    for (const auto& row : rows)
        if (row.found && row.train.sharpeRatio >= HELD_UP_SHARPE && row.test.sharpeRatio >= HELD_UP_SHARPE &&
            row.test.closedTrades >= minTestTrades && row.beatsBuyHold())
            out.push_back(row);
    std::stable_sort(out.begin(), out.end(),
                     [](const Row& a, const Row& b) { return a.test.sharpeRatio > b.test.sharpeRatio; });
    return out;
}

std::vector<Screener::Row>
Screener::topPicks(const std::vector<Row>& rows, size_t n, int minTestTrades)
{
    std::vector<Row> candidates = goodInBothPeriods(rows, minTestTrades);
    auto weaker = [](const Row& r) { return std::min(r.train.sharpeRatio, r.test.sharpeRatio); };
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&](const Row& a, const Row& b) { return weaker(a) > weaker(b); });

    std::vector<Row> picks;
    std::set<std::string> stocks;
    for (const auto& row : candidates)
    {
        if (picks.size() >= n)
            break;
        if (stocks.insert(row.symbol).second)   // one strategy per stock
            picks.push_back(row);
    }
    return picks;
}

json
Screener::strategySettings(const Row& pick) const
{
    json strategy = optimiser.currentSettings(pick.strategy);
    for (const auto& [key, value] : pick.parameters.items())
        strategy[key] = value;
    return strategy;
}

quant::PriceTable
Screener::dailyValues(const std::vector<Row>& picks, const std::map<std::string, std::vector<quant::DailyBar>>& daily) const
{
    std::map<std::string, std::vector<MarketCondition>> bars;
    for (const auto& [symbol, days] : daily)
        bars[symbol] = toBars(symbol, days);
    return dailyValues(picks, bars);
}

quant::PriceTable
Screener::dailyValues(const std::vector<Row>& picks, const std::map<std::string, std::vector<MarketCondition>>& allBars) const
{
    std::vector<std::string> names;
    std::map<std::string, std::vector<quant::DailyBar>> values;

    for (const auto& pick : picks)
    {
        auto found = allBars.find(pick.symbol);
        if (found == allBars.end() || found->second.empty())
            continue;

        json config = baseConfig;
        config["strategies"] = json::array({strategySettings(pick)});

        StrategyTester tester(config);
        tester.useCosts(TradingCosts(settings));
        tester.keepEquityCurve(true);
        const std::vector<MarketCondition>& bars = found->second;
        tester.runOn(bars);

        const auto& curve = tester.getEquityCurve();
        if (curve.size() < 2)
            continue;
        double capital = maxPosition * bars.front().Close;
        double start = curve.front().second;
        std::string name = pick.symbol + " " + pick.strategy;
        // One value per day: the last one (skipping the starting point, which repeats the first bar's time)
        std::vector<quant::DailyBar> series;
        for (size_t i = 1; i < curve.size(); i++)
        {
            std::string day = MarketTime::datePart(curve[i].first);
            double value = std::max(capital * LOWEST_VALUE_FRACTION, capital + curve[i].second - start);
            if (!series.empty() && series.back().date == day)
                series.back().close = value;
            else
            {
                quant::DailyBar point;
                point.date = day;
                point.close = value;
                series.push_back(point);
            }
        }
        names.push_back(name);
        values[name] = series;
    }
    return quant::alignCloses(names, values);
}

std::string
Screener::describe(const json& parameters)
{
    static const std::map<std::string, std::string> shortNames = {
        {"period", "period"}, {"oversold_threshold", "oversold"}, {"overbought_threshold", "overbought"},
        {"short_period", "short"}, {"long_period", "long"}, {"signal_period", "signal"}, {"signal_type", ""},
        {"lookback_period", "lookback"}, {"moving_average_period", "MA"}, {"entry_threshold", "entry"},
        {"exit_threshold", "exit"}, {"stop_loss", "SL"}, {"take_profit", "TP"},
        {"max_hold_bars", "hold"}, {"warmup_minutes", "warmup"}};

    std::ostringstream out;
    bool first = true;
    for (const auto& [key, value] : parameters.items())
    {
        auto found = shortNames.find(key);
        std::string label = found != shortNames.end() ? found->second : key;
        out << (first ? "" : " ") << label << (label.empty() ? "" : " ");
        if (value.is_string())
            out << value.get<std::string>();
        else if (value.is_number_integer())
            out << value.get<long long>();
        else if (value.is_number())
            out << value.get<double>();
        first = false;
    }
    return out.str();
}

// ---------- CSV ----------

static const char* CSV_HEADER =
    "symbol,strategy,found,combinations,train_days,test_days,test_start,test_end,parameters,"
    "train_sharpe,train_roi_pct,train_closed_trades,train_win_rate_pct,"
    "test_sharpe,test_roi_pct,test_return_on_max_position,test_closed_trades,test_win_rate_pct,test_total_pnl,"
    "buy_hold_return,buy_hold_sharpe,buy_hold_max_drawdown,beats_buy_hold";

static std::string quoted(const std::string& text)
{
    std::string out = "\"";
    for (char c : text)
        out += (c == '"') ? std::string("\"\"") : std::string(1, c);
    return out + "\"";
}

// Split one CSV line, honouring "quoted, fields" with "" for a quote inside
static std::vector<std::string> splitCsv(const std::string& line)
{
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); i++)
    {
        char c = line[i];
        if (inQuotes)
        {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { field += '"'; i++; }
            else if (c == '"') inQuotes = false;
            else field += c;
        }
        else if (c == '"') inQuotes = true;
        else if (c == ',') { fields.push_back(field); field.clear(); }
        else field += c;
    }
    fields.push_back(field);
    return fields;
}

bool
Screener::writeCsv(const std::string& path, const std::vector<Row>& rows)
{
    std::ostringstream file;
    file << CSV_HEADER << "\n" << std::setprecision(10);
    for (const auto& r : rows)
    {
        file << r.symbol << "," << r.strategy << "," << (r.found ? 1 : 0) << "," << r.combinations << ","
             << r.trainDays << "," << r.testDays << "," << r.testStart << "," << r.testEnd << ","
             << quoted(r.parameters.is_null() ? "{}" : r.parameters.dump()) << ","
             << r.train.sharpeRatio << "," << r.train.roiPercent << "," << r.train.closedTrades << ","
             << r.train.winRatePercent << ","
             << r.test.sharpeRatio << "," << r.test.roiPercent << "," << r.testReturnOnMaxPosition << ","
             << r.test.closedTrades << "," << r.test.winRatePercent << "," << r.test.totalPnL << ","
             << r.buyHoldReturn << "," << r.buyHoldSharpe << "," << r.buyHoldMaxDrawdown << ","
             << (r.beatsBuyHold() ? 1 : 0) << "\n";
    }
    return SaveData::write(path, file.str());
}

bool
Screener::writeTopPicksCsv(const std::string& path, const std::vector<Row>& picks)
{
    std::ostringstream file;
    file << "rank,symbol,strategy,parameters,train_sharpe,test_sharpe,buy_hold_sharpe,test_return_on_max_position,"
            "test_closed_trades\n";
    for (size_t i = 0; i < picks.size(); i++)
    {
        const Row& r = picks[i];
        file << i + 1 << "," << r.symbol << "," << r.strategy << "," << quoted(r.parameters.dump()) << ","
             << r.train.sharpeRatio << "," << r.test.sharpeRatio << "," << r.buyHoldSharpe << ","
             << r.testReturnOnMaxPosition << "," << r.test.closedTrades << "\n";
    }
    return SaveData::write(path, file.str());
}

std::vector<Screener::Row>
Screener::readCsv(const std::string& path)
{
    std::vector<Row> rows;
    std::ifstream file(path);
    std::string line;
    if (!file || !std::getline(file, line))
        return rows;

    std::vector<std::string> header = splitCsv(line);
    while (std::getline(file, line))
    {
        if (line.empty())
            continue;
        std::vector<std::string> values = splitCsv(line);
        if (values.size() != header.size())
            continue;
        std::map<std::string, std::string> f;
        for (size_t i = 0; i < header.size(); i++)
            f[header[i]] = values[i];

        auto number = [&](const std::string& key) {
            try { return std::stod(f[key]); } catch (const std::exception&) { return 0.0; }
        };
        Row r;
        r.symbol = f["symbol"];
        r.strategy = f["strategy"];
        r.found = f["found"] == "1";
        r.combinations = static_cast<int>(number("combinations"));
        r.trainDays = static_cast<int>(number("train_days"));
        r.testDays = static_cast<int>(number("test_days"));
        r.testStart = f["test_start"];
        r.testEnd = f["test_end"];
        r.parameters = json::parse(f["parameters"].empty() ? "{}" : f["parameters"], nullptr, false);
        r.train.sharpeRatio = number("train_sharpe");
        r.train.roiPercent = number("train_roi_pct");
        r.train.closedTrades = static_cast<int>(number("train_closed_trades"));
        r.train.winRatePercent = number("train_win_rate_pct");
        r.test.sharpeRatio = number("test_sharpe");
        r.test.roiPercent = number("test_roi_pct");
        r.testReturnOnMaxPosition = number("test_return_on_max_position");
        r.test.closedTrades = static_cast<int>(number("test_closed_trades"));
        r.test.winRatePercent = number("test_win_rate_pct");
        r.test.totalPnL = number("test_total_pnl");
        r.buyHoldReturn = number("buy_hold_return");
        r.buyHoldSharpe = number("buy_hold_sharpe");
        r.buyHoldMaxDrawdown = number("buy_hold_max_drawdown");
        rows.push_back(r);
    }
    return rows;
}
