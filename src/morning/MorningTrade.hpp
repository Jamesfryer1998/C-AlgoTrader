#pragma once

#include <string>

// One opening range breakout trade (OpeningRangeBreakout)
struct MorningTrade
{
    std::string symbol;
    std::string day;
    int side = 0;                 // +1 long, -1 short
    std::string entryTime;
    double entryPrice = 0;        // what we paid / received, slippage included
    double entryBeforeCosts = 0;  // the price the order filled at, before slippage
    double stopPrice = 0;
    std::string exitTime;
    double exitPrice = 0;         // slippage included
    double shares = 0;
    double grossPnl = 0;          // at the prices before slippage and fees
    double costs = 0;             // slippage and fees
    double netPnl = 0;            // gross - costs
    double riskDollars = 0;       // what the stop would have lost, before costs
    std::string exitReason;       // stop, target, time, close, end

    double r() const { return riskDollars > 0 ? netPnl / riskDollars : 0; }
};

// What one rule made on one stock on one day: a breakout's one trade, or a strategy's (SCALP's) several
struct StockDay
{
    std::string symbol;
    std::string day;
    int trades = 0;
    int wins = 0;                 // trades that made money after costs
    double grossPnl = 0;          // before slippage and fees
    double costs = 0;
    double netPnl = 0;

    static StockDay of(const MorningTrade& trade)
    {
        return {trade.symbol, trade.day, 1, trade.netPnl > 0 ? 1 : 0, trade.grossPnl, trade.costs, trade.netPnl};
    }
};
