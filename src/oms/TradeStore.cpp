#include "TradeStore.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include "../util/SaveData.hpp"

static const char* TRADES_FILE = "trades.csv";
static const char* POSITIONS_FILE = "positions.csv";
static const char* PERFORMANCE_FILE = "performance.json";
static const char* STATUS_FILE = "status.json";
static const char* EQUITY_FILE = "equity.csv";
static const char* EQUITY_HEADER = "time,equity";
static const int EQUITY_DECIMALS = 2;

// Upgrading a trades.csv from before the extra columns: the original is kept as trades.csv.old
static const char* OLD_FILE_SUFFIX = ".old";
static const char* UPGRADED_NOTE = "from older trades.csv format";

const std::vector<std::string> TradeStore::TRADE_COLUMNS = {
    "our_id", "time", "sent_at_utc", "strategy", "ticker", "side", "type", "order_class",
    "quantity", "price", "stop_loss", "take_profit", "status", "broker_status",
    "client_order_id", "broker_order_id", "round_trip_ms", "note", "order_path", "requests", "send_ms", "connect_ms"
};

namespace
{
    std::vector<std::string> splitCsv(const std::string& line)
    {
        std::vector<std::string> fields;
        std::stringstream stream(line);
        std::string field;
        while (std::getline(stream, field, ','))
            fields.push_back(field);
        if (!line.empty() && line.back() == ',')
            fields.push_back("");
        return fields;
    }

    std::string joinColumns(const std::vector<std::string>& columns)
    {
        std::string header;
        for (size_t i = 0; i < columns.size(); i++)
            header += (i ? "," : "") + columns[i];
        return header;
    }

    // Free text (error messages) must not break the CSV
    std::string clean(std::string text)
    {
        for (char& c : text)
            if (c == ',' || c == '\n' || c == '\r')
                c = ';';
        return text;
    }

    std::string formatTrade(const TradeRecord& t)
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision(4)
            << t.ourId << "," << t.time << "," << t.sentAtUtc << "," << clean(t.strategy) << ","
            << t.ticker << "," << t.side << "," << t.type << "," << t.orderClass << ","
            << t.quantity << "," << t.price << "," << t.stopLoss << "," << t.takeProfit << ","
            << t.status << "," << clean(t.brokerStatus) << "," << t.clientOrderId << "," << t.brokerOrderId << ","
            << std::setprecision(1) << t.roundTripMs << "," << clean(t.note) << "," << t.orderPath << "," << t.requests
            << "," << t.sendMs << "," << t.connectMs << "\n";
        return out.str();
    }

    double toDouble(const std::string& text)
    {
        try { return text.empty() ? 0.0 : std::stod(text); }
        catch (const std::exception&) { return 0.0; }
    }

    // Read trades.csv by column name, so files written with older column layouts still load
    std::vector<TradeRecord> readTrades(const std::string& filePath)
    {
        std::vector<TradeRecord> trades;
        std::ifstream file(filePath);
        std::string line;
        if (!std::getline(file, line))
            return trades;

        std::vector<std::string> header = splitCsv(line);
        std::map<std::string, size_t> column;
        for (size_t i = 0; i < header.size(); i++)
            column[header[i]] = i;

        int lineNumber = 0;
        while (std::getline(file, line))
        {
            if (line.empty())
                continue;
            lineNumber++;
            std::vector<std::string> f = splitCsv(line);
            auto get = [&](const std::string& name) -> std::string {
                auto it = column.find(name);
                return (it != column.end() && it->second < f.size()) ? f[it->second] : "";
            };

            TradeRecord t;
            t.ourId = static_cast<int>(toDouble(get("our_id")));
            if (t.ourId == 0)
                t.ourId = lineNumber;
            t.time = get("time");
            t.sentAtUtc = get("sent_at_utc");
            t.strategy = get("strategy");
            t.ticker = get("ticker");
            t.type = get("type");
            t.side = get("side");
            if (t.side.empty())   // older files had no side column
                t.side = (t.type == "BUY" || t.type == "LIMIT_BUY" || t.type == "STOP_BUY") ? BUY_SIDE : SELL_SIDE;
            t.orderClass = get("order_class");
            t.quantity = toDouble(get("quantity"));
            t.price = toDouble(get("price"));
            t.stopLoss = toDouble(get("stop_loss"));
            t.takeProfit = toDouble(get("take_profit"));
            t.status = get("status");
            t.brokerStatus = get("broker_status");
            t.clientOrderId = get("client_order_id");
            t.brokerOrderId = get("broker_order_id");
            t.roundTripMs = toDouble(get("round_trip_ms"));
            t.note = get("note");
            t.orderPath = get("order_path");                       // older files have none of these four
            t.requests = static_cast<int>(toDouble(get("requests")));
            t.sendMs = toDouble(get("send_ms"));
            t.connectMs = toDouble(get("connect_ms"));

            if (t.ticker.empty() || t.quantity <= 0)
            {
                std::cerr << "WARNING - Skipping bad line in trades.csv: " << line << std::endl;
                continue;
            }
            trades.push_back(t);
        }
        return trades;
    }
}

TradeStore::TradeStore(const std::string& dir)
: directory(dir)
{
    std::filesystem::create_directories(directory);
    upgradeOldTradesFile();
    writer = std::thread(&TradeStore::writerLoop, this);
}

TradeStore::~TradeStore()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    wake.notify_one();
    if (writer.joinable())
        writer.join();   // the loop finishes every queued job before exiting
}

std::string
TradeStore::path(const std::string& fileName) const
{
    return (std::filesystem::path(directory) / fileName).string();
}

// ---------- background writer ----------

void
TradeStore::enqueue(std::function<void()> job)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        jobs.push_back(std::move(job));
    }
    wake.notify_one();
}

void
TradeStore::writerLoop()
{
    std::unique_lock<std::mutex> lock(mutex);
    while (true)
    {
        wake.wait(lock, [this] { return stopping || !jobs.empty(); });
        if (jobs.empty() && stopping)
            return;

        std::function<void()> job = std::move(jobs.front());
        jobs.pop_front();
        busy = true;

        lock.unlock();
        job();            // the disk write, done without holding the lock
        lock.lock();

        busy = false;
        if (jobs.empty())
            idle.notify_all();
    }
}

void
TradeStore::flush()
{
    std::unique_lock<std::mutex> lock(mutex);
    idle.wait(lock, [this] { return jobs.empty() && !busy; });
}

// ---------- writes (queued) ----------

void
TradeStore::writeAtomically(const std::string& fileName, const std::string& contents)
{
    std::string finalPath = path(fileName);
    std::string tempPath = finalPath + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::trunc);
        if (!file)
        {
            std::cerr << "WARNING - Could not write " << tempPath << std::endl;
            return;
        }
        file << contents;
    }
    std::filesystem::rename(tempPath, finalPath);
    SaveData::saved(finalPath);
}

void
TradeStore::appendTrade(const TradeRecord& trade)
{
    std::string line = formatTrade(trade);   // format now, write later
    std::string filePath = path(TRADES_FILE);

    enqueue([filePath, line] {
        bool isNew = !std::filesystem::exists(filePath);
        std::ofstream file(filePath, std::ios::app);
        if (!file)
        {
            std::cerr << "WARNING - Could not write " << filePath << std::endl;
            return;
        }
        if (isNew)
            file << joinColumns(TRADE_COLUMNS) << "\n";
        file << line;
        file.close();
        SaveData::saved(filePath);
    });
}

void
TradeStore::savePositions(const std::vector<Position>& positions)
{
    std::ostringstream out;
    out << "ticker,quantity,avg_price\n" << std::fixed << std::setprecision(4);
    for (const auto& position : positions)
        out << position.getTicker() << "," << position.getQuantity() << "," << position.getAvgPrice() << "\n";

    std::string contents = out.str();
    enqueue([this, contents] { writeAtomically(POSITIONS_FILE, contents); });
}

void
TradeStore::savePerformance(const json& performance)
{
    std::string contents = performance.dump(2) + "\n";
    enqueue([this, contents] { writeAtomically(PERFORMANCE_FILE, contents); });
}

void
TradeStore::saveStatus(const json& status)
{
    std::string contents = status.dump(2) + "\n";
    enqueue([this, contents] { writeAtomically(STATUS_FILE, contents); });
}

void
TradeStore::appendEquity(const std::string& time, double equity)
{
    std::ostringstream line;
    line << time << "," << std::fixed << std::setprecision(EQUITY_DECIMALS) << equity << "\n";
    std::string filePath = path(EQUITY_FILE), text = line.str();
    enqueue([filePath, text] {
        bool isNew = !std::filesystem::exists(filePath);
        std::ofstream file(filePath, std::ios::app);
        if (!file)
        {
            std::cerr << "WARNING - Could not write " << filePath << std::endl;
            return;
        }
        if (isNew)
            file << EQUITY_HEADER << "\n";
        file << text;
        file.close();
        SaveData::saved(filePath);
    });
}

// ---------- reads (start-up only) ----------

// Files written before the extra columns existed are rewritten in the new layout, keeping every trade
void
TradeStore::upgradeOldTradesFile()
{
    std::string filePath = path(TRADES_FILE);
    std::ifstream file(filePath);
    std::string header;
    if (!file || !std::getline(file, header) || header == joinColumns(TRADE_COLUMNS))
        return;
    file.close();

    std::vector<TradeRecord> trades = readTrades(filePath);
    std::ostringstream out;
    out << joinColumns(TRADE_COLUMNS) << "\n";
    for (TradeRecord t : trades)
    {
        if (t.status.empty())
        {
            // Old files only held trades that changed the position. No stop loss/take profit = broker adjustment.
            t.status = (t.stopLoss == 0 && t.takeProfit == 0) ? TradeStatus::ADJUSTMENT : TradeStatus::SENT;
            t.note = UPGRADED_NOTE;
        }
        out << formatTrade(t);
    }
    std::filesystem::copy_file(filePath, filePath + OLD_FILE_SUFFIX, std::filesystem::copy_options::overwrite_existing);
    writeAtomically(TRADES_FILE, out.str());
    std::cout << "Upgraded " << filePath << " to the new column layout (" << trades.size()
              << " trade(s) kept, original saved as trades.csv.old)" << std::endl;
}

void
TradeStore::removeFiles(const std::string& directory)
{
    for (const char* file : {TRADES_FILE, POSITIONS_FILE, PERFORMANCE_FILE, STATUS_FILE, EQUITY_FILE})
        std::filesystem::remove(std::filesystem::path(directory) / file);
}

std::vector<TradeRecord>
TradeStore::loadTrades() const
{
    return readTrades(path(TRADES_FILE));
}

std::vector<Position>
TradeStore::loadPositions() const
{
    std::vector<Position> positions;
    std::ifstream file(path(POSITIONS_FILE));
    std::string line;
    std::getline(file, line); // header

    while (std::getline(file, line))
    {
        std::vector<std::string> f = splitCsv(line);
        if (f.size() != 3)
            continue;
        try
        {
            positions.emplace_back(f[0], std::stof(f[1]), std::stof(f[2]));
        }
        catch (const std::exception&)
        {
            std::cerr << "WARNING - Skipping bad line in positions.csv: " << line << std::endl;
        }
    }
    return positions;
}
