#pragma once

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

/**
 * A plain-text table with a title row and a dashed rule, columns sized to fit:
 *
 *   Stock  Strategy         Price   Lower
 *   -----  --------------  ------  ------
 *   TXN    MEANREV(60/60)  280.10  276.31
 *
 * Columns marked right-aligned suit numbers. Cells are UTF-8; width is counted in characters.
 */
class TextTable
{
    public:
        TextTable(std::vector<std::string> headers, std::vector<bool> rightAligned = {})
            : headers(std::move(headers)), right(std::move(rightAligned))
        {
            right.resize(this->headers.size(), false);
        }

        void addRow(std::vector<std::string> cells)
        {
            cells.resize(headers.size());
            rows.push_back(std::move(cells));
        }

        size_t size() const { return rows.size(); }

        // Columns at least this wide (characters), e.g. to keep a table repeated every minute the same size
        void setMinimumWidths(std::vector<size_t> widths)
        {
            minimumWidths = std::move(widths);
        }

        // Each column's width: its widest cell, header or minimum
        std::vector<size_t> columnWidths() const
        {
            std::vector<size_t> width(headers.size());
            for (size_t c = 0; c < headers.size(); c++)
            {
                width[c] = length(headers[c]);
                if (c < minimumWidths.size())
                    width[c] = std::max(width[c], minimumWidths[c]);
                for (const auto& row : rows)
                    width[c] = std::max(width[c], length(row[c]));
            }
            return width;
        }

        std::string render(const std::string& indent = "") const
        {
            std::vector<size_t> width = columnWidths();

            std::ostringstream out;
            auto line = [&](const std::vector<std::string>& cells) {
                std::string text = indent;
                for (size_t c = 0; c < cells.size(); c++)
                {
                    std::string pad(width[c] - length(cells[c]), ' ');
                    text += right[c] ? pad + cells[c] : cells[c] + pad;
                    if (c + 1 < cells.size())
                        text += "  ";
                }
                while (!text.empty() && text.back() == ' ')
                    text.pop_back();
                out << text << "\n";
            };

            line(headers);
            std::vector<std::string> rule;
            for (size_t c = 0; c < headers.size(); c++)
                rule.push_back(std::string(width[c], '-'));
            line(rule);
            for (const auto& row : rows)
                line(row);
            return out.str();
        }

        // Characters, not bytes (so "≥" counts as one)
        static size_t length(const std::string& text)
        {
            size_t n = 0;
            for (unsigned char c : text)
                if ((c & 0xC0) != 0x80)
                    n++;
            return n;
        }

    private:
        std::vector<std::string> headers;
        std::vector<bool> right;
        std::vector<std::vector<std::string>> rows;
        std::vector<size_t> minimumWidths;
};
