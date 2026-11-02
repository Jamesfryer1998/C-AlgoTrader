#include "DailyBars.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>

namespace quant
{
    bool writeDailyCsv(const std::string& path, const std::vector<DailyBar>& bars)
    {
        std::ofstream file(path);
        if (!file)
            return false;
        file << "Date,Open,High,Low,Close,Volume\n" << std::setprecision(10);
        for (const auto& b : bars)
            file << b.date << "," << b.open << "," << b.high << "," << b.low << "," << b.close << ","
                 << std::fixed << std::setprecision(0) << b.volume << std::defaultfloat << std::setprecision(10) << "\n";
        return static_cast<bool>(file);
    }

    std::vector<DailyBar> readDailyCsv(const std::string& path)
    {
        std::vector<DailyBar> bars;
        std::ifstream file(path);
        if (!file)
            return bars;

        std::string line;
        std::getline(file, line);   // header
        while (std::getline(file, line))
        {
            if (line.empty())
                continue;
            std::stringstream row(line);
            std::string field;
            std::vector<std::string> fields;
            while (std::getline(row, field, ','))
                fields.push_back(field);
            if (fields.size() < 6)
                continue;
            try
            {
                DailyBar b;
                b.date = fields[0];
                b.open = std::stod(fields[1]);
                b.high = std::stod(fields[2]);
                b.low = std::stod(fields[3]);
                b.close = std::stod(fields[4]);
                b.volume = std::stod(fields[5]);
                if (b.close > 0)
                    bars.push_back(b);
            }
            catch (const std::exception&)
            {
                // skip malformed rows
            }
        }
        std::sort(bars.begin(), bars.end(), [](const DailyBar& a, const DailyBar& b) { return a.date < b.date; });
        return bars;
    }

    PriceTable alignCloses(const std::vector<std::string>& symbols,
                           const std::map<std::string, std::vector<DailyBar>>& bars,
                           const std::string& startDate, const std::string& endDate)
    {
        PriceTable table;
        table.symbols = symbols;

        // Dates every stock traded on
        std::set<std::string> common;
        bool first = true;
        std::vector<std::map<std::string, double>> closeByDate;
        for (const auto& symbol : symbols)
        {
            std::map<std::string, double> closes;
            auto found = bars.find(symbol);
            if (found != bars.end())
                for (const auto& b : found->second)
                    if ((startDate.empty() || b.date >= startDate) && (endDate.empty() || b.date <= endDate))
                        closes[b.date] = b.close;

            std::set<std::string> dates;
            for (const auto& [date, close] : closes)
                dates.insert(date);
            if (first)
                common = dates;
            else
            {
                std::set<std::string> both;
                std::set_intersection(common.begin(), common.end(), dates.begin(), dates.end(),
                                      std::inserter(both, both.begin()));
                common = both;
            }
            first = false;
            closeByDate.push_back(closes);
        }

        table.dates.assign(common.begin(), common.end());
        table.closes.assign(symbols.size(), Vector());
        for (size_t s = 0; s < symbols.size(); s++)
            for (const auto& date : table.dates)
                table.closes[s].push_back(closeByDate[s].at(date));
        return table;
    }
}
