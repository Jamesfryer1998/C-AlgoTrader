#include <gtest/gtest.h>
#include "../../src/util/TextTable.hpp"

TEST(TextTableTests, ColumnsFitTheWidestCell)
{
    TextTable table({"Stock", "Price", "State"}, {false, true, false});
    table.addRow({"TXN", "280.10", "long, z -2.02"});
    table.addRow({"META", "1234.5", "flat"});
    EXPECT_EQ(table.render(),
              "Stock   Price  State\n"
              "-----  ------  -------------\n"
              "TXN    280.10  long, z -2.02\n"
              "META   1234.5  flat\n");
}

TEST(TextTableTests, MissingCellsAndUnicode)
{
    TextTable table({"A", "B"});
    table.addRow({"≥ 5"});   // one cell short: padded; "≥" is one character wide
    EXPECT_EQ(table.render("  "),
              "  A    B\n"
              "  ---  -\n"
              "  ≥ 5\n");
    EXPECT_EQ(TextTable::length("≥ 5"), 3u);
}

TEST(TextTableTests, MinimumWidthsKeepColumnsWide)
{
    TextTable table({"A", "B"}, {false, true});
    table.addRow({"x", "1"});
    table.setMinimumWidths({3, 4});
    EXPECT_EQ(table.columnWidths(), (std::vector<size_t>{3, 4}));
    EXPECT_EQ(table.render(), "A       B\n---  ----\nx       1\n");

    table.addRow({"long", "1"});   // a wider cell still widens its column
    EXPECT_EQ(table.columnWidths(), (std::vector<size_t>{4, 4}));
}
