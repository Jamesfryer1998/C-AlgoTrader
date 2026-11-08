#pragma once

#include <chrono>
#include <map>
#include <ostream>
#include <string>
#include <vector>
#include "AccountMonitor.hpp"
#include "../broker/AccountSnapshot.hpp"
#include "../data_access/MarketCondition.hpp"
#include "../strategy_engine/StrategyBase.hpp"

/**
 * MinuteReport
 *
 * The live app's output: one table per minute with a row per stock (and per strategy), the account in the
 * header, and what happened (orders, fills, rejections) listed underneath:
 *
 *   === 2026-11-05 15:57 New York | equity $99,674.35 | today -$325.65 ===
 *   Stock  Strategy        Price   Lower   Current  Upper   State          Position     Unrealised  Decision
 *   -----  --------------  ------  ------  -------  ------  -------------  -----------  ----------  --------
 *   DHR    MEANREV(20/60)  226.35  224.76  226.35   228.19  flat, z -0.21  -            -            0.08 ms
 *     DHR: Order sent to Alpaca: ...
 *
 * Bars for a minute arrive one stock at a time. The table is printed once every stock has reported, or once
 * the caller decides the stragglers aren't coming (isOverdue) - a quiet stock may have no bar in a minute.
 * Not thread-safe: the caller locks around it.
 */
class MinuteReport
{
    public:
        struct StockRow
        {
            double price = 0;
            std::vector<StrategyBase::DecisionLevels> strategies;
            PositionSnapshot position;
            double decisionMs = 0;
        };

        explicit MinuteReport(std::vector<std::string> tickers);

        // A stock's row for the bar's minute. If the bar starts a new minute, the unfinished one is printed first.
        void addRow(const MarketCondition& bar, const StockRow& row, std::ostream& out);
        void addEvent(const std::string& ticker, const std::string& text);
        void setAccount(const AccountSnapshot& account, const std::string& headerNote);

        bool hasRows() const { return !minute.empty(); }
        bool isComplete() const { return rows.size() == tickers.size(); }
        bool isOverdue(std::chrono::steady_clock::duration wait) const;

        // Print the minute's table and start afresh (nothing printed if no rows)
        void print(std::ostream& out);

    private:
        std::vector<std::string> tickers;   // portfolio order
        std::string minute;                 // the bars' time, e.g. "2026-11-05 15:57:00"
        std::chrono::steady_clock::time_point firstRowAt;
        std::map<std::string, StockRow> rows;
        std::map<std::string, double> lastPrice;   // kept across minutes, for stocks with no bar
        std::vector<std::string> events;
        std::vector<size_t> columnWidths;   // only ever grow, so the table keeps one size
        AccountSnapshot account;
        std::string headerNote;
};
