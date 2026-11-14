#include "MorningData.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include "../alpaca/AlpacaApi.hpp"
#include "../util/MarketTime.hpp"
#include "../util/SaveData.hpp"

namespace fs = std::filesystem;

namespace
{
    const std::string SYMBOLS_FILE = "symbols.txt";
    const std::string DAILY_FOLDER = "daily";
    const std::string OPENING_FOLDER = "opening";
    const std::string MINUTE_FOLDER = "minute";
    const std::string DAILY_PROGRESS_FILE = ".downloaded.json";   // in daily/: the range and the symbols done
    const std::string CSV = ".csv";
    const std::string PARTIAL = ".partial";

    const std::string BARS_HEADER = "Symbol,Time,Open,High,Low,Close,Volume";
    const std::string ASKED_PREFIX = "# asked: ";   // minute/<DAY>.csv's first line: every symbol asked for
    const int PRICE_DECIMALS = 4;

    // Stocks per request: a request returns at most 10,000 bars a page, so fewer stocks for longer spans
    const size_t DAILY_SYMBOLS_PER_REQUEST = 100;     // ~800 days each: pages of 10,000
    const size_t OPENING_SYMBOLS_PER_REQUEST = 500;   // one bar each
    const size_t MINUTE_SYMBOLS_PER_REQUEST = 25;     // 390 bars each: one page

    const std::string MINUTES = "Min";   // Alpaca's timeframe, e.g. "5Min"
    const std::time_t ONE_SECOND = 1;

    std::string joinSymbols(const std::vector<std::string>& symbols)
    {
        std::string joined;
        for (const auto& symbol : symbols)
            joined += (joined.empty() ? "" : ",") + symbol;
        return joined;
    }

    std::vector<std::string> splitSymbols(const std::string& joined)
    {
        std::vector<std::string> symbols;
        std::stringstream stream(joined);
        std::string symbol;
        while (std::getline(stream, symbol, ','))
            if (!symbol.empty())
                symbols.push_back(symbol);
        return symbols;
    }

    // Write all of a file or none of it: readers never see half a file
    bool writeWhole(const std::string& path, const std::string& contents)
    {
        fs::create_directories(fs::path(path).parent_path());
        std::string partial = path + PARTIAL;
        {
            std::ofstream file(partial);
            if (!file)
                return false;
            file << contents;
            if (!file)
                return false;
        }
        std::error_code error;
        fs::rename(partial, path, error);
        if (error)
            return false;
        SaveData::saved(path);
        return true;
    }

    void writeBar(std::ostringstream& out, const std::string& symbol, const IntradayBar& bar)
    {
        out << symbol << "," << bar.time << "," << bar.open << "," << bar.high << "," << bar.low << ","
            << bar.close << "," << static_cast<long long>(bar.volume) << "\n";
    }

    // "AAPL,2026-10-28 09:30:00,1,2,0.5,1.5,100" -> symbol and bar; false if malformed
    bool readBar(const std::string& line, std::string& symbol, IntradayBar& bar)
    {
        std::stringstream stream(line);
        std::string field;
        std::vector<std::string> fields;
        while (std::getline(stream, field, ','))
            fields.push_back(field);
        if (fields.size() != 7)
            return false;
        try
        {
            symbol = fields[0];
            bar.time = fields[1];
            bar.open = std::stod(fields[2]);
            bar.high = std::stod(fields[3]);
            bar.low = std::stod(fields[4]);
            bar.close = std::stod(fields[5]);
            bar.volume = std::stod(fields[6]);
        }
        catch (const std::exception&)
        {
            return false;
        }
        return true;
    }

    std::string utcOf(std::time_t time)
    {
        return MarketTime::formatUtc(time);
    }
}

MorningData::MorningData(const json& config, const std::string& folder, bool offline,
                         std::shared_ptr<AlpacaClient> client)
    : alpaca(config, client), folder(folder), offline(offline)
{
}

std::string MorningData::dailyPath(const std::string& symbol) const
{
    return (fs::path(folder) / DAILY_FOLDER / (symbol + CSV)).string();
}

std::string MorningData::openingPath(const std::string& day) const
{
    return (fs::path(folder) / OPENING_FOLDER / (day + CSV)).string();
}

std::string MorningData::minutePath(const std::string& day) const
{
    return (fs::path(folder) / MINUTE_FOLDER / (day + CSV)).string();
}

bool
MorningData::symbols(std::vector<std::string>& out)
{
    std::string path = (fs::path(folder) / SYMBOLS_FILE).string();
    std::ifstream saved(path);
    std::string symbol;
    out.clear();
    while (std::getline(saved, symbol))
        if (!symbol.empty())
            out.push_back(symbol);
    if (!out.empty() || offline)
        return !out.empty();

    bool ok = false;
    out = alpaca.tradableSymbols(ok);
    requests++;
    if (!ok || out.empty())
        return false;
    std::string contents;
    for (const auto& listed : out)
        contents += listed + "\n";
    writeWhole(path, contents);
    return true;
}

bool
MorningData::ensureDailyBars(const std::vector<std::string>& symbols, const std::string& from, const std::string& to,
                             const std::function<void(size_t, size_t)>& progress)
{
    // Which symbols already have this range
    std::string progressPath = (fs::path(folder) / DAILY_FOLDER / DAILY_PROGRESS_FILE).string();
    std::set<std::string> done;
    {
        std::ifstream file(progressPath);
        json saved = json::parse(file, nullptr, false);
        if (saved.is_object() && saved.value("from", "") == from && saved.value("to", "") == to)
            for (const auto& symbol : saved.value("symbols", json::array()))
                done.insert(symbol.get<std::string>());
    }
    std::vector<std::string> missing;
    for (const auto& symbol : symbols)
        if (!done.count(symbol))
            missing.push_back(symbol);
    if (missing.empty() || offline)
        return missing.empty();

    std::string startUtc = utcOf(MarketTime::parseDate(from));
    std::string endUtc = utcOf(MarketTime::parseDate(to) + MarketTime::SECONDS_PER_DAY - ONE_SECOND);
    bool allOk = true;
    for (size_t first = 0; first < missing.size(); first += DAILY_SYMBOLS_PER_REQUEST)
    {
        std::vector<std::string> batch(missing.begin() + first,
                                       missing.begin() + std::min(missing.size(), first + DAILY_SYMBOLS_PER_REQUEST));
        bool ok = false;
        BarsBySymbol bars = alpaca.fetch(batch, AlpacaApi::ONE_DAY, startUtc, endUtc, batch.size(), ok);
        requests++;
        if (!ok)
        {
            allOk = false;
            continue;
        }
        for (const auto& symbol : batch)
        {
            std::vector<quant::DailyBar> daily;
            for (const auto& bar : bars[symbol])
                daily.push_back({MarketTime::datePart(bar.time), bar.open, bar.high, bar.low, bar.close, bar.volume});
            fs::create_directories(fs::path(dailyPath(symbol)).parent_path());
            quant::writeDailyCsv(dailyPath(symbol), daily);
            SaveData::saved(dailyPath(symbol));
            done.insert(symbol);
        }
        json saved = {{"from", from}, {"to", to}, {"symbols", done}};
        writeWhole(progressPath, saved.dump());
        if (progress)
            progress(std::min(missing.size(), first + batch.size()), missing.size());
    }
    return allOk;
}

std::vector<quant::DailyBar>
MorningData::dailyBars(const std::string& symbol) const
{
    return quant::readDailyCsv(dailyPath(symbol));
}

std::map<std::string, IntradayBar>
MorningData::readOpenings(const std::string& path)
{
    std::map<std::string, IntradayBar> bars;
    std::ifstream file(path);
    std::string line, symbol;
    std::getline(file, line);   // header
    while (std::getline(file, line))
    {
        IntradayBar bar;
        if (readBar(line, symbol, bar))
            bars[symbol] = bar;
    }
    return bars;
}

bool
MorningData::writeOpenings(const std::string& path, const std::map<std::string, IntradayBar>& bars)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(PRICE_DECIMALS) << BARS_HEADER << "\n";
    for (const auto& [symbol, bar] : bars)
        writeBar(out, symbol, bar);
    return writeWhole(path, out.str());
}

std::map<std::string, IntradayBar>
MorningData::openingBars(const std::string& day, const std::vector<std::string>& symbols, int openingMinutes)
{
    std::string path = openingPath(day);
    if (fs::exists(path) || offline)
        return readOpenings(path);

    std::time_t open = MarketTime::nextRegularOpen(MarketTime::parseDate(day));
    bool ok = false;
    BarsBySymbol bars = alpaca.fetch(symbols, std::to_string(openingMinutes) + MINUTES, utcOf(open),
                                     utcOf(open + ONE_SECOND), OPENING_SYMBOLS_PER_REQUEST, ok);
    requests += (symbols.size() + OPENING_SYMBOLS_PER_REQUEST - 1) / OPENING_SYMBOLS_PER_REQUEST;
    std::map<std::string, IntradayBar> openings;
    for (const auto& [symbol, list] : bars)
        if (!list.empty() && MarketTime::minuteOfDay(list.front().time) == MarketTime::OPEN_MINUTE)
            openings[symbol] = list.front();
    if (ok)
        writeOpenings(path, openings);   // a failed request is asked again next run
    return openings;
}

MorningData::BarsBySymbol
MorningData::readMinutes(const std::string& path, std::vector<std::string>& asked)
{
    BarsBySymbol bars;
    std::ifstream file(path);
    std::string line, symbol;
    if (!std::getline(file, line) || line.rfind(ASKED_PREFIX, 0) != 0)
        return bars;
    asked = splitSymbols(line.substr(ASKED_PREFIX.size()));
    std::getline(file, line);   // header
    while (std::getline(file, line))
    {
        IntradayBar bar;
        if (readBar(line, symbol, bar))
            bars[symbol].push_back(bar);
    }
    return bars;
}

bool
MorningData::writeMinutes(const std::string& path, const BarsBySymbol& bars, const std::vector<std::string>& asked)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(PRICE_DECIMALS) << ASKED_PREFIX << joinSymbols(asked) << "\n"
        << BARS_HEADER << "\n";
    for (const auto& [symbol, list] : bars)
        for (const auto& bar : list)
            writeBar(out, symbol, bar);
    return writeWhole(path, out.str());
}

MorningData::BarsBySymbol
MorningData::minuteBars(const std::string& day, const std::vector<std::string>& symbols)
{
    std::string path = minutePath(day);
    std::vector<std::string> asked;
    BarsBySymbol bars = readMinutes(path, asked);
    std::set<std::string> already(asked.begin(), asked.end());
    std::vector<std::string> missing;
    for (const auto& symbol : symbols)
        if (!already.count(symbol))
            missing.push_back(symbol);
    if (missing.empty() || offline)
        return bars;

    std::time_t open = MarketTime::nextRegularOpen(MarketTime::parseDate(day));
    std::time_t close = MarketTime::regularCloseAfter(open);
    bool ok = false;
    BarsBySymbol fetched = alpaca.fetch(missing, AlpacaApi::ONE_MINUTE, utcOf(open), utcOf(close - ONE_SECOND),
                                        MINUTE_SYMBOLS_PER_REQUEST, ok);
    requests += (missing.size() + MINUTE_SYMBOLS_PER_REQUEST - 1) / MINUTE_SYMBOLS_PER_REQUEST;
    for (auto& [symbol, list] : fetched)
        bars[symbol] = std::move(list);
    if (ok)
    {
        asked.insert(asked.end(), missing.begin(), missing.end());
        writeMinutes(path, bars, asked);
    }
    return bars;
}
