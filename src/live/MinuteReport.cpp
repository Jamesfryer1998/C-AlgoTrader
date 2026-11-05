#include "MinuteReport.hpp"

#include "../util/Format.hpp"
#include "../util/TextTable.hpp"

namespace
{
    const size_t TIME_WITHOUT_SECONDS = 16;   // "2026-11-05 15:57:00" -> "2026-11-05 15:57"

    std::string orDash(const std::string& text)
    {
        return text.empty() ? "-" : text;
    }
}

MinuteReport::MinuteReport(std::vector<std::string> tickers) : tickers(std::move(tickers))
{
}

void
MinuteReport::addRow(const MarketCondition& bar, const StockRow& row, std::ostream& out)
{
    if (hasRows() && bar.DateTime != minute)
        print(out);   // a new minute started before every stock reported
    if (!hasRows())
    {
        minute = bar.DateTime;
        firstRowAt = std::chrono::steady_clock::now();
    }
    rows[bar.Ticker] = row;
    lastPrice[bar.Ticker] = bar.Close;
}

void
MinuteReport::addEvent(const std::string& ticker, const std::string& text)
{
    events.push_back(ticker + ": " + text);
}

void
MinuteReport::setAccount(const AccountSnapshot& snapshot, const std::string& note)
{
    if (snapshot.available)
        account = snapshot;
    headerNote = note;
}

bool
MinuteReport::isOverdue(std::chrono::steady_clock::duration wait) const
{
    return hasRows() && std::chrono::steady_clock::now() - firstRowAt > wait;
}

void
MinuteReport::print(std::ostream& out)
{
    if (!hasRows())
        return;

    out << "\n=== " << minute.substr(0, TIME_WITHOUT_SECONDS) << " New York";
    if (account.available)
        out << " | equity " << Format::dollars(account.equity) << " | today " << Format::dollarChange(account.todayPnL());
    if (!headerNote.empty())
        out << " | " << headerNote;
    out << " ===\n";

    TextTable table({"Stock", "Strategy", "Price", "Lower", "Current", "Upper", "State", "Position", "Unrealised", "Decision"},
                    {false, false, true, true, true, true, false, false, true, true});
    for (const auto& ticker : tickers)
    {
        auto found = rows.find(ticker);
        if (found == rows.end())
        {
            auto last = lastPrice.find(ticker);
            table.addRow({ticker, "", last != lastPrice.end() ? Format::number(last->second) : "-", "", "", "",
                          "no bar this minute"});
            continue;
        }

        const StockRow& row = found->second;
        for (size_t i = 0; i < row.strategies.size(); i++)
        {
            const StrategyBase::Levels& levels = row.strategies[i];
            bool firstLine = i == 0;   // stock-wide columns only on a stock's first line
            table.addRow({firstLine ? ticker : "", levels.label, firstLine ? Format::number(row.price) : "",
                          orDash(levels.lower), orDash(levels.current), orDash(levels.upper), levels.state,
                          firstLine ? row.position.describe() : "", firstLine ? row.position.describePnL() : "",
                          firstLine ? Format::number(row.decisionMs) + " ms" : ""});
        }
    }
    out << table.render();
    for (const auto& event : events)
        out << "  " << event << "\n";
    out << std::flush;

    minute.clear();
    rows.clear();
    events.clear();
}

void
MinuteReport::printLegend(std::ostream& out)
{
    out << "\nEach minute: one row per stock. Lower / Upper are the levels that make a strategy trade (buy at or\n"
        << "below Lower, sell at or above Upper); Current is what's compared with them: the price, or the RSI or\n"
        << "MACD value. \"exit\" marks where an open trade will be closed. Orders and errors are listed underneath.\n"
        << std::flush;
}
