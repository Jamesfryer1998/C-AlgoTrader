#include <gtest/gtest.h>
#include <sstream>
#include "../../src/live/MinuteReport.hpp"

namespace
{
    MarketCondition bar(const std::string& ticker, const std::string& time, float close)
    {
        return MarketCondition(time, ticker, close, close, 1000, "1m");
    }

    MinuteReport::StockRow row(double price, double quantity = 0, double averagePrice = 0)
    {
        MinuteReport::StockRow stock;
        stock.price = price;
        StrategyBase::DecisionLevels levels;
        levels.label = "MEANREV(10/10)";
        levels.ready = true;
        levels.current = std::to_string(price);
        levels.state = "flat, z +0.10";
        stock.strategies.push_back(levels);
        stock.position.quantity = quantity;
        stock.position.averagePrice = averagePrice;
        return stock;
    }

    // The table's lines, without the blank ones
    std::vector<std::string> lines(const std::string& text)
    {
        std::vector<std::string> out;
        std::istringstream stream(text);
        for (std::string line; std::getline(stream, line);)
            if (!line.empty())
                out.push_back(line);
        return out;
    }
}

TEST(MinuteReportTests, PrintsOnceEveryStockHasReported)
{
    MinuteReport report({"KO", "PEP"});
    std::ostringstream out;
    report.addRow(bar("KO", "2026-10-26 10:00:00", 60.5), row(60.5), out);
    EXPECT_FALSE(report.isComplete());
    report.addRow(bar("PEP", "2026-10-26 10:00:00", 150.25), row(150.25), out);
    EXPECT_TRUE(report.isComplete());
    EXPECT_TRUE(out.str().empty());   // the caller decides when to print

    report.print(out);
    std::string text = out.str();
    EXPECT_NE(text.find("2026-10-26 10:00"), std::string::npos);
    EXPECT_NE(text.find("Market Value"), std::string::npos);
    EXPECT_LT(text.find("KO "), text.find("PEP "));   // portfolio order
    EXPECT_FALSE(report.hasRows());                    // starts afresh
}

TEST(MinuteReportTests, ANewMinutePrintsTheUnfinishedOne)
{
    MinuteReport report({"KO", "PEP"});
    std::ostringstream out;
    report.addRow(bar("KO", "2026-10-26 10:00:00", 60.5), row(60.5), out);
    report.addRow(bar("KO", "2026-10-26 10:01:00", 60.6), row(60.6), out);   // PEP never reported for 10:00
    EXPECT_NE(out.str().find("2026-10-26 10:00"), std::string::npos);
    EXPECT_EQ(out.str().find("10:01"), std::string::npos);
}

TEST(MinuteReportTests, MarketValueOfAPosition)
{
    MinuteReport report({"KO"});
    std::ostringstream out;
    MinuteReport::StockRow held = row(60.5, 10, 60.0);
    held.position.unrealisedPnL = 5;            // the account monitor works these out
    held.position.unrealisedFraction = 5.0 / 600;
    report.addRow(bar("KO", "2026-10-26 10:00:00", 60.5), held, out);
    report.print(out);
    EXPECT_NE(out.str().find("$605.00"), std::string::npos);   // Market Value: 10 shares at $60.50
    EXPECT_NE(out.str().find("10 @ 60.00"), std::string::npos);
    EXPECT_NE(out.str().find("+$5.00 (+0.83%)"), std::string::npos);
}

TEST(MinuteReportTests, EventsAreListedUnderTheTable)
{
    MinuteReport report({"KO"});
    std::ostringstream out;
    report.addRow(bar("KO", "2026-10-26 10:00:00", 60.5), row(60.5), out);
    report.addEvent("KO", "Order sent: BUY 10");
    report.print(out);
    std::vector<std::string> printed = lines(out.str());
    ASSERT_FALSE(printed.empty());
    EXPECT_NE(printed.back().find("KO: Order sent: BUY 10"), std::string::npos);
}

TEST(MinuteReportTests, TheTableKeepsOneWidth)
{
    // A wider value one minute must not make the next minute's table narrower again
    MinuteReport report({"KO"});
    std::ostringstream first, second;
    report.addRow(bar("KO", "2026-10-26 10:00:00", 60.5), row(60.5, 1000, 60.0), first);
    report.print(first);
    report.addRow(bar("KO", "2026-10-26 10:01:00", 60.5), row(60.5), second);
    report.print(second);

    auto heading = [](const std::string& text) {
        for (const auto& line : lines(text))
            if (line.rfind("Stock", 0) == 0)
                return line;
        return std::string();
    };
    ASSERT_FALSE(heading(first.str()).empty());
    EXPECT_EQ(heading(second.str()), heading(first.str()));
}

TEST(PositionSnapshotTests, DescribesAPositionOrFlat)
{
    PositionSnapshot flat;
    EXPECT_EQ(flat.describe(), "-");
    EXPECT_EQ(flat.describeValue(100), "-");
    EXPECT_EQ(flat.describePnL(), "-");

    PositionSnapshot shortTwo;
    shortTwo.quantity = -2;
    shortTwo.averagePrice = 100;
    shortTwo.unrealisedPnL = 4;
    shortTwo.unrealisedFraction = 0.02;
    EXPECT_EQ(shortTwo.describe(), "-2 @ 100.00");
    EXPECT_EQ(shortTwo.describeValue(98), "-$196.00");
    EXPECT_EQ(shortTwo.describePnL(), "+$4.00 (+2.00%)");
}
