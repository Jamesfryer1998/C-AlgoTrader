#include "Portfolio.hpp"

#include <set>
#include <stdexcept>

namespace
{
    // The whole config as one stock sees it: its own ticker and strategies, no portfolio
    json stockConfig(const json& config, const std::string& ticker, const json& strategies)
    {
        json stock = config;
        stock.erase("portfolio");
        stock["ticker"] = ticker;
        stock["strategies"] = strategies;
        return stock;
    }
}

std::vector<PortfolioEntry>
Portfolio::fromConfig(const json& config)
{
    std::vector<PortfolioEntry> entries;
    json portfolio = config.value("portfolio", json::array());

    if (!portfolio.is_array() || portfolio.empty())
        throw std::runtime_error("there's no \"portfolio\": list each stock with its \"strategy\"");

    std::set<std::string> seen;
    for (size_t i = 0; i < portfolio.size(); i++)
    {
        const json& item = portfolio[i];
        std::string where = "portfolio entry " + std::to_string(i + 1);
        if (!item.is_object() || !item.contains("ticker") || !item["ticker"].is_string()
            || item["ticker"].get<std::string>().empty())
            throw std::runtime_error(where + " needs a \"ticker\"");

        PortfolioEntry entry;
        entry.ticker = item["ticker"].get<std::string>();
        if (!seen.insert(entry.ticker).second)
            throw std::runtime_error(entry.ticker + " is in the portfolio twice");

        json strategies = json::array();
        if (item.contains("strategy") && item["strategy"].is_object())
            strategies.push_back(item["strategy"]);
        if (item.contains("strategies") && item["strategies"].is_array())
            for (const auto& s : item["strategies"])
                strategies.push_back(s);
        if (strategies.empty())
            throw std::runtime_error(entry.ticker + " in the portfolio needs a \"strategy\" or \"strategies\"");
        for (auto& s : strategies)
        {
            if (!s.is_object() || !s.contains("name"))
                throw std::runtime_error(entry.ticker + " has a strategy without a \"name\"");
            if (!s.contains("active"))
                s["active"] = 1;
        }

        entry.config = stockConfig(config, entry.ticker, strategies);
        entries.push_back(entry);
    }
    return entries;
}

PortfolioEntry
Portfolio::forTicker(const json& config, const std::string& ticker)
{
    if (!config.value("portfolio", json::array()).empty())
        for (const auto& entry : fromConfig(config))
            if (entry.ticker == ticker)
                return entry;
    return {ticker, stockConfig(config, ticker, json::array())};
}
