#include "MorningReport.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include "../quant/DeflatedSharpe.hpp"
#include "../util/MarketTime.hpp"
#include "../util/SaveData.hpp"

namespace fs = std::filesystem;

namespace
{
    const double PERCENT = 100.0;
    const int FILLS_PER_ROUND_TRIP = 2;
    const std::string HOLDOUT_FILE = "holdout_opened.json";
    const std::string STEP_1 = "Step 1: moves against costs";
    const std::string STEP_3 = "Step 3: the breakout as published";
    const std::string STEP_4 = "Step 4: short holds";
    const std::string STEP_5 = "Step 5: the holdout";
    const std::string STEP_1_PERIODS = "development + validation";
    const int TABLE_NAME_WIDTH = 28;
    const int TABLE_COLUMN_WIDTH = 10;
    const int DECIMALS = 2;
    const int PRICE_DECIMALS = 4;
    const int MOVE_DECIMALS = 3;     // a 10-minute move is a few hundredths of a percent
    const int RATIO_DECIMALS = 1;

    std::string fixed(double value, int decimals = DECIMALS)
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision(decimals) << value;
        return out.str();
    }

    bool isAsPublished(const MorningSettings::Variant& variant)
    {
        return !variant.isStrategy() && variant.holdMinutes == 0 && variant.targetR == 0;
    }

    // The line's returns on the days in `period`
    quant::Vector returnsIn(const MorningBacktest::Line& line, const MorningSettings& settings, const std::string& period)
    {
        quant::Vector returns;
        for (size_t i = 0; i < line.days.size(); i++)
            if (settings.periodOf(line.days[i]) == period)
                returns.push_back(line.returns[i]);
        return returns;
    }

    json movesJson(const MorningBacktest::Moves& moves)
    {
        return {{"stock_days", moves.stockDays}, {"move_10_minutes_percent", moves.move10Percent},
                {"move_30_minutes_percent", moves.move30Percent}, {"atr_percent", moves.atrPercent},
                {"relative_volume", moves.relativeVolume}};
    }
}

MorningReport::MorningReport(const MorningSettings& settings, const MorningBacktest::Results& results)
    : settings(settings), results(results)
{
    periods = {MorningSettings::DEVELOPMENT, MorningSettings::VALIDATION};
    if (results.holdoutTraded)
        periods.push_back(MorningSettings::HOLDOUT);
    for (const auto& period : periods)
    {
        for (const auto& line : results.lines)
            table.push_back(rowFor(line, period));
        addDeflatedSharpes(period);
    }
    addGates();
}

MorningReport::Row
MorningReport::rowFor(const MorningBacktest::Line& line, const std::string& period) const
{
    Row row;
    row.variant = line.variant;
    row.selection = line.selection;
    row.period = period;
    quant::Performance performance = quant::performance(returnsIn(line, settings, period));
    row.days = performance.days;
    row.totalReturn = performance.totalReturn;
    row.cagr = performance.cagr;
    row.sharpe = performance.sharpe;
    row.maxDrawdown = performance.maxDrawdown;

    int wins = 0;
    double net = 0, gross = 0, costs = 0;
    for (const auto& stockDay : line.stockDays)
        if (settings.periodOf(stockDay.day) == period)
        {
            row.trades += stockDay.trades;
            wins += stockDay.wins;
            net += stockDay.netPnl;
            gross += stockDay.grossPnl;
            costs += stockDay.costs;
        }
    if (row.trades > 0)
    {
        row.winRate = static_cast<double>(wins) / row.trades;
        row.avgNetPerTrade = net / row.trades;
        row.avgCostPerTrade = costs / row.trades;
    }
    row.grossToCost = costs > 0 ? gross / costs : 0;
    return row;
}

const MorningReport::Row*
MorningReport::find(const std::string& variant, const std::string& selection, const std::string& period) const
{
    for (const auto& row : table)
        if (row.variant == variant && row.selection == selection && row.period == period)
            return &row;
    return nullptr;
}

void
MorningReport::addDeflatedSharpes(const std::string& period)
{
    quant::Vector trials;
    for (const auto& line : results.lines)
        if (line.selection == MorningBacktest::IN_PLAY)
            trials.push_back(quant::dailySharpe(returnsIn(line, settings, period)));
    for (const auto& line : results.lines)
        if (line.selection == MorningBacktest::IN_PLAY)
            for (auto& row : table)
                if (row.variant == line.variant && row.selection == line.selection && row.period == period)
                    row.deflatedSharpe = quant::deflatedSharpe(returnsIn(line, settings, period), trials);
}

double
MorningReport::moveToCost() const
{
    auto inPlay = results.moves.find(MorningBacktest::IN_PLAY);
    double roundTrip = FILLS_PER_ROUND_TRIP * settings.slippagePercent;
    return inPlay == results.moves.end() || roundTrip <= 0 ? 0 : inPlay->second.move10Percent / roundTrip;
}

bool
MorningReport::Gate::pass() const
{
    return !checks.empty() && std::all_of(checks.begin(), checks.end(), [](const Check& check) { return check.pass; });
}

void
MorningReport::addGates()
{
    Gate step1{STEP_1, "", STEP_1_PERIODS, {}};
    double ratio = moveToCost();
    step1.checks.push_back({"In-play stocks' 10-minute move at least " + fixed(settings.minMoveToCost, 0) + "x the round-trip cost",
                            ratio >= settings.minMoveToCost, fixed(ratio, RATIO_DECIMALS) + "x"});
    verdicts.push_back(step1);

    auto judge = [&](const std::string& step, const MorningSettings::Variant& variant, const std::string& period,
                     bool holdout) {
        const Row* inPlay = find(variant.name, MorningBacktest::IN_PLAY, period);
        const Row* random = find(variant.name, MorningBacktest::RANDOM, period);
        if (!inPlay || !random)
            return;
        Gate gate{step, variant.name, period, {}};
        gate.checks.push_back({"Sharpe at least " + fixed(settings.minSharpe, RATIO_DECIMALS), inPlay->sharpe >= settings.minSharpe,
                               fixed(inPlay->sharpe)});
        double lead = inPlay->sharpe - random->sharpe;
        gate.checks.push_back({"At least " + fixed(settings.minLeadOverRandom, RATIO_DECIMALS) + " Sharpe ahead of random stocks",
                               lead >= settings.minLeadOverRandom, fixed(lead) + " (random " + fixed(random->sharpe) + ")"});
        gate.checks.push_back({"Earns more per trade than it costs", inPlay->avgNetPerTrade > 0,
                               "$" + fixed(inPlay->avgNetPerTrade) + " a trade after $" + fixed(inPlay->avgCostPerTrade)
                               + " of costs"});
        if (holdout)
        {
            gate.checks.push_back({"At least " + std::to_string(settings.minTrades) + " trades",
                                   inPlay->trades >= settings.minTrades, std::to_string(inPlay->trades)});
            gate.checks.push_back({"Deflated Sharpe at least " + fixed(settings.minDeflatedSharpe),
                                   inPlay->deflatedSharpe >= settings.minDeflatedSharpe, fixed(inPlay->deflatedSharpe)});
        }
        verdicts.push_back(gate);
    };
    for (const auto& variant : settings.variants)
        judge(isAsPublished(variant) ? STEP_3 : STEP_4, variant, MorningSettings::VALIDATION, false);
    if (results.holdoutTraded)
        for (const auto& variant : settings.variants)
            judge(STEP_5, variant, MorningSettings::HOLDOUT, true);
}

json
MorningReport::toJson(const std::string& holdoutOpened, size_t downloads) const
{
    json report;
    report["generated_utc"] = MarketTime::formatUtc(std::time(nullptr));
    report["dates"] = {{"start", settings.start}, {"development_end", settings.developmentEnd},
                       {"validation_end", settings.validationEnd}, {"end", settings.end}};
    report["holdout_opened"] = holdoutOpened.empty() ? json() : json(holdoutOpened);
    report["holdout_traded"] = results.holdoutTraded;
    report["universe_symbols"] = results.symbols;
    report["days_traded"] = results.daysTraded;
    report["downloads"] = downloads;
    report["stocks_a_day"] = settings.stocks;
    report["round_trip_cost_percent"] = FILLS_PER_ROUND_TRIP * settings.slippagePercent;

    json moves = json::object();
    for (const auto& [selection, measured] : results.moves)
        moves[selection] = movesJson(measured);
    report["step1"] = {{"moves", moves}, {"move_to_cost", moveToCost()}};

    json rows = json::array();
    for (const auto& row : table)
        rows.push_back({{"variant", row.variant}, {"selection", row.selection}, {"period", row.period},
                        {"days", row.days}, {"trades", row.trades}, {"win_rate", row.winRate},
                        {"total_return", row.totalReturn}, {"cagr", row.cagr}, {"sharpe", row.sharpe},
                        {"max_drawdown", row.maxDrawdown}, {"avg_net_per_trade", row.avgNetPerTrade},
                        {"avg_cost_per_trade", row.avgCostPerTrade}, {"gross_to_cost", row.grossToCost},
                        {"deflated_sharpe", row.deflatedSharpe >= 0 ? json(row.deflatedSharpe) : json()}});
    report["results"] = rows;

    json gates = json::array();
    for (const auto& gate : verdicts)
    {
        json checks = json::array();
        for (const auto& check : gate.checks)
            checks.push_back({{"name", check.name}, {"pass", check.pass}, {"detail", check.detail}});
        gates.push_back({{"step", gate.step}, {"variant", gate.variant}, {"period", gate.period},
                         {"pass", gate.pass()}, {"checks", checks}});
    }
    report["gates"] = gates;

    // Each line's account, day by day, from 1
    json curves = json::array();
    for (const auto& line : results.lines)
    {
        json points = json::array();
        double value = 1;
        for (size_t i = 0; i < line.days.size(); i++)
        {
            value *= 1 + line.returns[i];
            points.push_back({line.days[i], value});
        }
        curves.push_back({{"variant", line.variant}, {"selection", line.selection}, {"points", points}});
    }
    report["curves"] = curves;
    return report;
}

bool
MorningReport::writeTrades(const std::string& path) const
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(PRICE_DECIMALS)
        << "variant,selection,symbol,day,side,entry_time,entry,stop,exit_time,exit,shares,gross_pnl,costs,net_pnl,r,exit_reason\n";
    for (const auto& line : results.lines)
        for (const auto& trade : line.trades)
            if (settings.periodOf(trade.day) != MorningSettings::HOLDOUT || results.holdoutTraded)
                out << '"' << line.variant << "\"," << line.selection << "," << trade.symbol << "," << trade.day << ","
                    << (trade.side > 0 ? "long" : "short") << "," << trade.entryTime << "," << trade.entryPrice << ","
                    << trade.stopPrice << "," << trade.exitTime << "," << trade.exitPrice << "," << trade.shares << ","
                    << trade.grossPnl << "," << trade.costs << "," << trade.netPnl << "," << trade.r() << ","
                    << trade.exitReason << "\n";
    return SaveData::write(path, out.str());
}

void
MorningReport::print(std::ostream& out) const
{
    out << "\nMORNING SCREEN  " << settings.start << " to " << settings.end << "  (" << results.daysTraded
        << " days traded, " << settings.stocks << " stocks a day)\n";
    for (const auto& [selection, moves] : results.moves)
        out << "  " << std::left << std::setw(TABLE_COLUMN_WIDTH) << selection << " typical 10-minute move "
            << fixed(moves.move10Percent, MOVE_DECIMALS) << "%, 30-minute " << fixed(moves.move30Percent, MOVE_DECIMALS) << "%, ATR "
            << fixed(moves.atrPercent) << "% of the price, over " << moves.stockDays << " stock-days\n";

    for (const auto& period : periods)
    {
        out << "\n  " << period << "\n  " << std::left << std::setw(TABLE_NAME_WIDTH) << "rule"
            << std::setw(TABLE_COLUMN_WIDTH) << "stocks" << std::right << std::setw(TABLE_COLUMN_WIDTH) << "return"
            << std::setw(TABLE_COLUMN_WIDTH) << "Sharpe" << std::setw(TABLE_COLUMN_WIDTH) << "drawdown"
            << std::setw(TABLE_COLUMN_WIDTH) << "trades" << std::setw(TABLE_COLUMN_WIDTH) << "$/trade"
            << std::setw(TABLE_COLUMN_WIDTH) << "deflated" << "\n";
        for (const auto& row : table)
            if (row.period == period)
                out << "  " << std::left << std::setw(TABLE_NAME_WIDTH) << row.variant.substr(0, TABLE_NAME_WIDTH - 1)
                    << std::setw(TABLE_COLUMN_WIDTH) << row.selection.substr(0, TABLE_COLUMN_WIDTH - 1) << std::right
                    << std::setw(TABLE_COLUMN_WIDTH) << fixed(row.totalReturn * PERCENT, RATIO_DECIMALS) + "%"
                    << std::setw(TABLE_COLUMN_WIDTH) << fixed(row.sharpe)
                    << std::setw(TABLE_COLUMN_WIDTH) << fixed(row.maxDrawdown * PERCENT, RATIO_DECIMALS) + "%"
                    << std::setw(TABLE_COLUMN_WIDTH) << row.trades
                    << std::setw(TABLE_COLUMN_WIDTH) << fixed(row.avgNetPerTrade)
                    << std::setw(TABLE_COLUMN_WIDTH) << (row.deflatedSharpe >= 0 ? fixed(row.deflatedSharpe) : "") << "\n";
    }

    out << "\n  GATES\n";
    for (const auto& gate : verdicts)
    {
        out << "  " << (gate.pass() ? "PASS  " : "FAIL  ") << gate.step
            << (gate.variant.empty() ? "" : " - " + gate.variant) << " (" << gate.period << ")\n";
        for (const auto& check : gate.checks)
            out << "          " << (check.pass ? "ok  " : "no  ") << check.name << ": " << check.detail << "\n";
    }
    out << std::endl;
}

namespace MorningHoldout
{
    std::string openedAt(const std::string& folder)
    {
        std::ifstream file((fs::path(folder) / HOLDOUT_FILE).string());
        json saved = json::parse(file, nullptr, false);
        return saved.is_object() ? saved.value("opened_utc", std::string()) : std::string();
    }

    std::string open(const std::string& folder, const std::vector<std::string>& variants)
    {
        std::string already = openedAt(folder);
        if (!already.empty())
            return already;
        std::string now = MarketTime::formatUtc(std::time(nullptr));
        json record = {{"opened_utc", now}, {"variants", variants}};
        fs::create_directories(folder);
        SaveData::write((fs::path(folder) / HOLDOUT_FILE).string(), record.dump(2));
        return now;
    }
}
