#include "PairsBacktest.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <set>
#include "PositionBook.hpp"
#include "../morning/StockSelection.hpp"

namespace
{
    const int TRADING_DAYS_A_MONTH = 21;
    const int LEGS = 2;

    // One pair being traded in one month's set
    struct Traded
    {
        PairsBacktest::Pair pair;
        size_t lastDay = 0;            // its set's final day: anything open is closed then
        double baseA = 0, baseB = 0;   // prices the day before trading began
        long trade = -1;               // the open trade, -1 when flat
        int openSign = 0;              // +1: opened with a above b
        long actionOn = -1;            // a day an open or close is due (delay_days after its signal)
        int actionSign = 0;            // +1/-1 an open, 0 a close
        bool dead = false;             // a stock stopped trading
    };

    int signOf(double value)
    {
        return value > 0 ? 1 : value < 0 ? -1 : 0;
    }

    // Short the one above, buy the one below
    std::vector<PositionBook::LegOrder> legsFor(const PairsBacktest::Pair& pair, int sign)
    {
        return {{pair.a, -sign}, {pair.b, sign}};
    }
}

double
PairsBacktest::gapSigma(size_t a, size_t b, size_t formationEnd) const
{
    size_t from = formationEnd - settings.formationDays;
    double firstA = prices.close[a][from], firstB = prices.close[b][from];
    double sum = 0, squares = 0;
    for (size_t day = from; day < formationEnd; day++)
    {
        double gap = prices.close[a][day] / firstA - prices.close[b][day] / firstB;
        sum += gap;
        squares += gap * gap;
    }
    double n = settings.formationDays, mean = sum / n;
    return std::sqrt(std::max(0.0, squares / n - mean * mean));
}

std::vector<PairsBacktest::Pair>
PairsBacktest::closestPairs(const std::vector<size_t>& universe, size_t formationEnd) const
{
    size_t from = formationEnd - settings.formationDays;
    std::vector<std::vector<double>> normalised;
    for (size_t stock : universe)
    {
        std::vector<double> path;
        for (size_t day = from; day < formationEnd; day++)
            path.push_back(prices.close[stock][day] / prices.close[stock][from]);
        normalised.push_back(std::move(path));
    }
    struct Distance { double squares; size_t i, j; };
    std::vector<Distance> distances;
    for (size_t i = 0; i < universe.size(); i++)
        for (size_t j = i + 1; j < universe.size(); j++)
        {
            double squares = 0;
            for (size_t day = 0; day < normalised[i].size(); day++)
            {
                double gap = normalised[i][day] - normalised[j][day];
                squares += gap * gap;
            }
            distances.push_back({squares, i, j});
        }
    std::sort(distances.begin(), distances.end(), [](const Distance& x, const Distance& y) {
        return x.squares != y.squares ? x.squares < y.squares : std::tie(x.i, x.j) < std::tie(y.i, y.j);
    });

    std::vector<Pair> pairs;
    std::set<size_t> used;
    for (const auto& distance : distances)
    {
        if (pairs.size() >= settings.pairs)
            break;
        size_t a = universe[distance.i], b = universe[distance.j];
        if (used.count(a) || used.count(b))
            continue;
        used.insert(a);
        used.insert(b);
        pairs.push_back({a, b, gapSigma(a, b, formationEnd)});
    }
    return pairs;
}

std::vector<PairsBacktest::Pair>
PairsBacktest::randomPairs(const std::vector<size_t>& universe, size_t formationEnd) const
{
    std::vector<size_t> shuffled = universe;
    std::sort(shuffled.begin(), shuffled.end());
    std::mt19937_64 random(StockSelection::stableHash(prices.days[formationEnd]) ^ settings.randomSeed);
    std::shuffle(shuffled.begin(), shuffled.end(), random);
    std::vector<Pair> pairs;
    for (size_t i = 0; i + 1 < shuffled.size() && pairs.size() < settings.pairs; i += LEGS)
        pairs.push_back({shuffled[i], shuffled[i + 1], gapSigma(shuffled[i], shuffled[i + 1], formationEnd)});
    return pairs;
}

void
PairsBacktest::run(const PairsSettings::Variant& variant, bool matched, size_t first, size_t last,
                   MorningBacktest::Line& line) const
{
    PositionBook book(prices, costs);
    std::vector<Traded> traded;
    std::vector<size_t> starts = prices.monthStarts(first);
    std::set<size_t> monthStart(starts.begin(), starts.end());
    int setsAtOnce = (settings.tradingDays + TRADING_DAYS_A_MONTH - 1) / TRADING_DAYS_A_MONTH;

    for (size_t day = first; day <= last && day < prices.days.size(); day++)
    {
        double morning = line.equity;
        double pnl = book.mark(day);
        double legValue = settings.leverage * morning / (setsAtOnce * settings.pairs * LEGS);

        // A new month: a new set of pairs from the year before
        if (monthStart.count(day) && day >= static_cast<size_t>(settings.formationDays))
        {
            std::vector<size_t> universe = prices.liquid(day, settings.formationDays, settings.minPrice,
                                                         settings.minDollarVolume, settings.universeSize);
            for (const auto& pair : matched ? closestPairs(universe, day) : randomPairs(universe, day))
            {
                Traded entry;
                entry.pair = pair;
                entry.lastDay = day + settings.tradingDays - 1;
                entry.baseA = prices.close[pair.a][day - 1];
                entry.baseB = prices.close[pair.b][day - 1];
                traded.push_back(entry);
            }
        }

        // Open (sign +1/-1) or close (sign 0) a pair now
        auto act = [&](Traded& pair, int sign) {
            if (sign != 0 && pair.trade < 0)
            {
                pair.trade = book.open(legsFor(pair.pair, sign), legValue, day, pnl);
                pair.openSign = sign;
            }
            else if (sign == 0 && pair.trade >= 0)
            {
                book.close(pair.trade, line, pnl);
                pair.trade = -1;
            }
        };

        for (auto& pair : traded)
        {
            if (pair.dead)
                continue;
            double a = prices.close[pair.pair.a][day], b = prices.close[pair.pair.b][day];
            if (a <= 0 || b <= 0)   // stopped trading: out at its last price
            {
                act(pair, 0);
                pair.dead = true;
                continue;
            }
            if (pair.actionOn == static_cast<long>(day))   // decided delay_days ago
            {
                act(pair, pair.actionSign);
                pair.actionOn = -1;
            }
            double gap = a / pair.baseA - b / pair.baseB;
            bool wide = pair.trade < 0 && pair.pair.sigma > 0 && std::fabs(gap) > settings.openSigma * pair.pair.sigma;
            bool closed = pair.trade >= 0 && signOf(gap) != pair.openSign;
            if (pair.actionOn < 0 && day < pair.lastDay && (wide || closed))
            {
                int sign = wide ? signOf(gap) : 0;
                if (variant.delayDays == 0)
                    act(pair, sign);
                else
                {
                    pair.actionOn = static_cast<long>(day) + variant.delayDays;
                    pair.actionSign = sign;
                }
            }
            if (day >= pair.lastDay)   // the set's time is up
            {
                act(pair, 0);
                pair.dead = true;
            }
        }
        traded.erase(std::remove_if(traded.begin(), traded.end(), [](const Traded& pair) { return pair.dead; }),
                     traded.end());

        line.days.push_back(prices.days[day]);
        line.returns.push_back(morning > 0 ? pnl / morning : 0);
        line.equity += pnl;
    }
}
