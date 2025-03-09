#pragma once
#include <iostream>
#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

class StrategyAttribute 
{
    public:
        StrategyAttribute() = default;

        // Load from JSON constructor
        StrategyAttribute(const json& stratJson) {
            if (stratJson.contains("period")) period = stratJson["period"];
            if (stratJson.contains("overbought_threshold")) overbought_threshold = stratJson["overbought_threshold"];
            if (stratJson.contains("oversold_threshold")) oversold_threshold = stratJson["oversold_threshold"];

            if (stratJson.contains("short_period")) short_period = stratJson["short_period"];
            if (stratJson.contains("long_period")) long_period = stratJson["long_period"];
            if (stratJson.contains("signal_period")) signal_period = stratJson["signal_period"];

            if (stratJson.contains("lookback_period")) lookback_period = stratJson["lookback_period"];
            if (stratJson.contains("entry_threshold")) entry_threshold = stratJson["entry_threshold"];
            if (stratJson.contains("exit_threshold")) exit_threshold = stratJson["exit_threshold"];
            if (stratJson.contains("moving_average_period")) moving_average_period = stratJson["moving_average_period"];

            if (stratJson.contains("name")) name = stratJson["name"];
            if (stratJson.contains("price_type")) price_type = stratJson["price_type"];
            if (stratJson.contains("trade_size")) trade_size = stratJson["trade_size"];
        }

        // RSI
        int period;
        double overbought_threshold;
        double oversold_threshold;

        // MACD
        int short_period;
        int long_period;
        int signal_period;

        // MEANREV
        int lookback_period;
        double entry_threshold;
        double exit_threshold;
        int moving_average_period;

        // Common
        string name;
        string price_type = "close";
        double trade_size = 1.0;  // Default to 100% of capital
};