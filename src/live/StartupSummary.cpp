#include "StartupSummary.hpp"

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>

namespace
{
    const std::string TITLE = " AlgoTrader ";
    const char RULE = '=';
    const size_t MIN_RULE_WIDTH = 80;
    const int LABEL_WIDTH = 10;
    const std::string COLUMN_GAP = "  ";
    const std::string NOT_SET = "-";
    const std::string SETTING_SEPARATOR = ", ";

    // Strategy keys that have their own column, or aren't settings
    const std::string NAME_KEY = "name";
    const std::string STOP_KEY = "stop_loss";
    const std::string TARGET_KEY = "take_profit";
    const std::set<std::string> NOT_SETTINGS = {NAME_KEY, "active", STOP_KEY, TARGET_KEY};

    const std::vector<std::string> COLUMNS = {"Stock", "Strategy", "Stop", "Target", "History", "Settings"};

    // As written in the config, without trailing zeros: 60, 0.5, 3 (for 3.0)
    std::string value(const json& setting)
    {
        if (setting.is_string())
            return setting.get<std::string>();
        if (setting.is_number_float())
        {
            std::ostringstream text;
            text << setting.get<double>();
            return text.str();
        }
        return setting.dump();
    }

    std::string percentSetting(const json& strategy, const std::string& key)
    {
        return strategy.contains(key) ? value(strategy[key]) + "%" : NOT_SET;
    }
}

void
StartupSummary::add(const std::string& label, const std::string& text)
{
    if (!text.empty())
        header.emplace_back(label, text);
}

void
StartupSummary::addStock(const std::string& ticker, const json& strategies, size_t backfilled, int barsNeeded)
{
    std::string history = std::to_string(backfilled) + "/" + std::to_string(barsNeeded);
    bool first = true;
    for (const auto& strategy : strategies)
    {
        rows.push_back({first ? ticker : "", strategy.value(NAME_KEY, NOT_SET), percentSetting(strategy, STOP_KEY),
                        percentSetting(strategy, TARGET_KEY), first ? history : "", describeSettings(strategy)});
        first = false;
    }
    if (first)
        rows.push_back({ticker, NOT_SET, NOT_SET, NOT_SET, history, ""});
}

void
StartupSummary::print(std::ostream& out) const
{
    // Column widths: the widest of the heading and every row
    std::vector<size_t> widths;
    for (const auto& heading : COLUMNS)
        widths.push_back(heading.size());
    for (const auto& row : rows)
    {
        std::vector<std::string> cells = {row.ticker, row.strategy, row.stop, row.target, row.history};
        for (size_t i = 0; i < cells.size(); i++)
            widths[i] = std::max(widths[i], cells[i].size());
    }

    auto printRow = [&](const std::vector<std::string>& cells, std::ostream& line)
    {
        // Names on the left, numbers on the right, settings last (as long as they need)
        line << std::left << std::setw(static_cast<int>(widths[0])) << cells[0] << COLUMN_GAP
             << std::setw(static_cast<int>(widths[1])) << cells[1] << COLUMN_GAP << std::right
             << std::setw(static_cast<int>(widths[2])) << cells[2] << COLUMN_GAP
             << std::setw(static_cast<int>(widths[3])) << cells[3] << COLUMN_GAP
             << std::setw(static_cast<int>(widths[4])) << cells[4] << COLUMN_GAP << std::left << cells[5];
    };

    std::vector<std::string> lines;
    for (const auto& [label, text] : header)
    {
        std::ostringstream line;
        line << std::left << std::setw(LABEL_WIDTH) << label << text;
        lines.push_back(line.str());
    }
    lines.push_back("");
    std::ostringstream headings;
    printRow(COLUMNS, headings);
    lines.push_back(headings.str());
    for (const auto& row : rows)
    {
        std::ostringstream line;
        printRow({row.ticker, row.strategy, row.stop, row.target, row.history, row.settings}, line);
        lines.push_back(line.str());
    }

    size_t width = MIN_RULE_WIDTH;
    for (const auto& line : lines)
        width = std::max(width, line.size());
    size_t left = (width - TITLE.size()) / 2;

    out << "\n" << std::string(left, RULE) << TITLE << std::string(width - left - TITLE.size(), RULE) << "\n";
    for (const auto& line : lines)
        out << line << "\n";
    out << std::string(width, RULE) << std::endl;
}

std::string
StartupSummary::describeRisk(const json& risk)
{
    std::string settings;
    for (const auto& [key, setting] : risk.items())
        if (key != NAME_KEY)
            settings += (settings.empty() ? "" : SETTING_SEPARATOR) + key + " " + value(setting);
    std::string name = risk.value(NAME_KEY, NOT_SET);
    return settings.empty() ? name : name + ": " + settings;
}

std::string
StartupSummary::describeSettings(const json& strategy)
{
    std::string settings;
    for (const auto& [key, setting] : strategy.items())
        if (!NOT_SETTINGS.count(key))
            settings += (settings.empty() ? "" : SETTING_SEPARATOR) + key + " " + value(setting);
    return settings;
}
