#include "IntradayUniverse.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace
{
    const std::string BENCHMARK = "SPY";

    double mean(const std::vector<double>& values)
    {
        double sum = 0;
        for (double value : values)
            sum += value;
        return values.empty() ? 0 : sum / values.size();
    }

    double covariance(const std::vector<double>& a, const std::vector<double>& b)
    {
        double meanA = mean(a), meanB = mean(b), sum = 0;
        for (size_t i = 0; i < a.size() && i < b.size(); i++)
            sum += (a[i] - meanA) * (b[i] - meanB);
        return a.size() > 1 ? sum / (a.size() - 1) : 0;
    }

    // Daily returns over [from, to), or empty if a bar is missing
    std::vector<double> returns(const PriceMatrix& prices, size_t stock, size_t from, size_t to)
    {
        std::vector<double> out;
        for (size_t day = from; day < to; day++)
        {
            double before = prices.close[stock][day - 1], after = prices.close[stock][day];
            if (before <= 0 || after <= 0)
                return {};
            out.push_back(after / before - 1);
        }
        return out;
    }
}

namespace IntradayUniverse
{
    double correlation(const std::vector<double>& a, const std::vector<double>& b)
    {
        double spread = std::sqrt(covariance(a, a) * covariance(b, b));
        return spread > 0 ? covariance(a, b) / spread : 0;
    }

    double beta(const std::vector<double>& stock, const std::vector<double>& fund)
    {
        double variance = covariance(fund, fund);
        return variance > 0 ? covariance(stock, fund) / variance : 0;
    }

    double residualSigma(const std::vector<double>& stock, const std::vector<double>& fund, double beta)
    {
        std::vector<double> residual;
        for (size_t i = 0; i < stock.size() && i < fund.size(); i++)
            residual.push_back(stock[i] - beta * fund[i]);
        return std::sqrt(covariance(residual, residual));
    }

    std::vector<Member> pick(const PriceMatrix& prices, const IntradaySettings& settings, size_t monthStart)
    {
        size_t days = static_cast<size_t>(settings.formationDays);
        if (monthStart <= days || monthStart > prices.days.size())
            return {};
        size_t from = monthStart - days + 1;   // returns need the close before

        // The funds (and SPY) with a full year
        std::vector<std::pair<size_t, std::vector<double>>> funds;
        std::set<size_t> notStocks;
        for (const auto& symbol : settings.funds)
        {
            size_t row = prices.row(symbol);
            if (row >= prices.stocks())
                continue;
            notStocks.insert(row);
            std::vector<double> fundReturns = returns(prices, row, from, monthStart);
            if (!fundReturns.empty())
                funds.push_back({row, fundReturns});
        }
        if (funds.empty())
            return {};
        std::vector<std::vector<double>> trackers;
        for (const auto& [row, fundReturns] : funds)
            trackers.push_back(fundReturns);
        size_t spy = prices.row(BENCHMARK);
        if (spy < prices.stocks())
        {
            notStocks.insert(spy);
            std::vector<double> spyReturns = returns(prices, spy, from, monthStart);
            if (!spyReturns.empty())
                trackers.push_back(spyReturns);
        }
        for (const auto& symbol : settings.notStocks)
            notStocks.insert(prices.row(symbol));

        // The rest by dollar volume, most first
        std::vector<std::pair<double, size_t>> ranked;
        for (size_t stock = 0; stock < prices.stocks(); stock++)
        {
            if (notStocks.count(stock) || prices.close[stock][monthStart - 1] <= settings.minPrice)
                continue;
            double dollars = 0;
            for (size_t day = from - 1; day < monthStart; day++)
                dollars += prices.dollarVolume[stock][day];
            ranked.push_back({dollars, stock});
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });

        std::vector<Member> members;
        std::vector<std::vector<double>> picked;
        for (const auto& [dollars, stock] : ranked)
        {
            if (members.size() >= settings.stocks)
                break;
            std::vector<double> stockReturns = returns(prices, stock, from, monthStart);
            if (stockReturns.empty())
                continue;
            auto tooClose = [&](const std::vector<double>& other) {
                return correlation(stockReturns, other) > settings.maxCorrelation;
            };
            if (std::any_of(trackers.begin(), trackers.end(), tooClose)
                || std::any_of(picked.begin(), picked.end(), tooClose))
                continue;

            size_t best = 0;
            for (size_t f = 1; f < funds.size(); f++)
                if (correlation(stockReturns, funds[f].second) > correlation(stockReturns, funds[best].second))
                    best = f;
            Member member;
            member.stock = stock;
            member.fund = funds[best].first;
            member.beta = beta(stockReturns, funds[best].second);
            member.dailySigma = residualSigma(stockReturns, funds[best].second, member.beta);
            if (member.dailySigma <= 0)
                continue;
            members.push_back(member);
            picked.push_back(stockReturns);
        }
        return members;
    }
}
