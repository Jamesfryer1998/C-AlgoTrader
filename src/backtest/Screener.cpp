#include "Screener.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include "Backtester.hpp"
#include <sstream>
#include "../quant/Stats.hpp"

static json withDailyBars(json config)
{
    config["bars_per_year"] = quant::TRADING_DAYS;   // Sharpe ratios annualised for daily bars
    return config;
}

static json pickOnlyTheBest(json settings)
{
    settings["top_n_to_test"] = 1;   // only the training pick is ever run on the test years
    return settings;
}

Screener::Screener(const json& baseConfig, const json& settings)
    : baseConfig(withDailyBars(baseConfig)),
      settings(settings),
      optimiser(withDailyBars(baseConfig), pickOnlyTheBest(settings))
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
        bars.emplace_back(d.date + " 16:00:00", symbol, static_cast<float>(d.open), static_cast<float>(d.close),
                          volume, "1d");
    }
    return bars;
}

std::vector<Screener::Row>
Screener::screenStock(const std::string& symbol, const std::vector<quant::DailyBar>& daily,
                      const std::vector<std::string>& strategies, int threads,
                      const std::function<void(const std::string&, size_t, size_t)>& onProgress) const
{
    std::string testStart = settings.value("test_start", "2024-01-01");
    size_t minTrainDays = settings.value("min_train_days", 500);
    size_t minTestDays = settings.value("min_test_days", 120);
    size_t warmupWanted = settings.value("test_warmup_bars", 320);
    double maxPosition = baseConfig.value("max_position_size", 10.0);

    // 1. Split: training years before test_start, test years from it. The test run is given the last
    //    training bars first as history only, so strategies start the test years warmed up.
    std::vector<MarketCondition> all = toBars(symbol, daily);
    std::vector<MarketCondition> train, test;
    for (const auto& bar : all)
        (bar.DateTime.substr(0, 10) < testStart ? train : test).push_back(bar);

    std::vector<Row> rows;
    for (const auto& name : strategies)
    {
        Row row;
        row.symbol = symbol;
        row.strategy = name;
        row.trainDays = static_cast<int>(train.size());
        row.testDays = static_cast<int>(test.size());
        if (!test.empty())
        {
            row.testStart = test.front().DateTime.substr(0, 10);
            row.testEnd = test.back().DateTime.substr(0, 10);
        }
        rows.push_back(row);
    }
    if (train.size() < minTrainDays || test.size() < minTestDays)
        return rows;   // not enough history (e.g. listed recently): nothing found

    // 2. Buy and hold over the test days, the benchmark every strategy has to beat
    quant::Vector closes;
    for (const auto& bar : test)
        closes.push_back(bar.Close);
    quant::Performance buyHold = quant::performance(quant::simpleReturns(closes));
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
Screener::topByTraining(const std::vector<Row>& rows, size_t n)
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
Screener::heldUp(const std::vector<Row>& rows, double minTrainSharpe, double minTestSharpe, int minTestTrades)
{
    std::vector<Row> out;
    for (const auto& row : rows)
        if (row.found && row.train.sharpeRatio >= minTrainSharpe && row.test.sharpeRatio >= minTestSharpe &&
            row.test.closedTrades >= minTestTrades && row.beatsBuyHold())
            out.push_back(row);
    std::stable_sort(out.begin(), out.end(),
                     [](const Row& a, const Row& b) { return a.test.sharpeRatio > b.test.sharpeRatio; });
    return out;
}

std::vector<Screener::Row>
Screener::topPicks(const std::vector<Row>& rows, size_t n, int minTestTrades)
{
    std::vector<Row> candidates = heldUp(rows, 0.5, 0.5, minTestTrades);
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

quant::PriceTable
Screener::pickValues(const std::vector<Row>& picks, const std::map<std::string, std::vector<quant::DailyBar>>& daily) const
{
    double maxPosition = baseConfig.value("max_position_size", 10.0);
    std::vector<std::string> names;
    std::map<std::string, std::vector<quant::DailyBar>> values;

    for (const auto& pick : picks)
    {
        auto found = daily.find(pick.symbol);
        if (found == daily.end() || found->second.empty())
            continue;

        // The chosen setting on top of the strategy's other settings
        json strategySettings = optimiser.currentSettings(pick.strategy);
        for (const auto& [key, value] : pick.parameters.items())
            strategySettings[key] = value;
        json config = baseConfig;
        config["strategies"] = json::array({strategySettings});

        Backtester backtester(config);
        backtester.setStartingCapital(settings.value("starting_capital", 100000.0));
        backtester.setCommissionPerTrade(settings.value("commission_per_trade", 0.0));
        backtester.setSlippagePercentage(settings.value("slippage_percent", 0.01) / 100.0);
        backtester.getBroker().setSlippageMode(SimulatedBroker::SlippageMode::Adverse);
        backtester.keepEquityCurve(true);
        std::vector<MarketCondition> bars = toBars(pick.symbol, found->second);
        backtester.runOn(bars);

        const auto& curve = backtester.getEquityCurve();
        if (curve.size() < 2)
            continue;
        double capital = maxPosition * bars.front().Close;
        double start = curve.front().second;
        std::string name = pick.symbol + " " + pick.strategy;
        std::vector<quant::DailyBar> series;
        for (size_t i = 1; i < curve.size(); i++)   // skip the starting point (it repeats the first date)
        {
            quant::DailyBar point;
            point.date = curve[i].first.substr(0, 10);
            point.close = std::max(capital * 0.01, capital + curve[i].second - start);
            series.push_back(point);
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
        {"exit_threshold", "exit"}, {"stop_loss", "SL"}, {"take_profit", "TP"}};

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
    std::ofstream file(path);
    if (!file)
        return false;
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
    return static_cast<bool>(file);
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
