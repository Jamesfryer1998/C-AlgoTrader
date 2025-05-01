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

            short_period = stratJson.contains("short_period") ? stratJson["short_period"].get<int>() : 0;
            long_period = stratJson.contains("long_period") ? stratJson["long_period"].get<int>() : 0;
            signal_period = stratJson.contains("signal_period") ? stratJson["signal_period"].get<int>() : 0;

            // MEANREV
            lookback_period = stratJson.contains("lookback_period") ? stratJson["lookback_period"].get<int>() : 0;
            entry_threshold = stratJson.contains("entry_threshold") ? stratJson["entry_threshold"].get<double>() : 0;
            exit_threshold = stratJson.contains("exit_threshold") ? stratJson["exit_threshold"].get<double>() : 0;
            moving_average_period = stratJson.contains("moving_average_period") ? stratJson["moving_average_period"].get<int>() : 0;

            if (stratJson.contains("name")) name = stratJson["name"];
            if (stratJson.contains("price_type")) price_type = stratJson["price_type"];
            if (stratJson.contains("trade_size")) trade_size = stratJson["trade_size"];
            if (stratJson.contains("stop_loss")) stop_loss = stratJson["stop_loss"];
            if (stratJson.contains("take_profit")) take_profit = stratJson["take_profit"];
        }

        // RSI
        int period;
        double oversold_threshold;
        double overbought_threshold;

        // MACD
        int short_period;
        int long_period;
        int signal_period;

        // MEANREV
        int lookback_period;
        double exit_threshold;
        double entry_threshold;
        int moving_average_period;

        // Common
        string name;
        string price_type = "close";
        double trade_size = 1.0;  // Default to 100% of capital
        int stop_loss = 10; // Default to 10% stop loss
        int take_profit = 5; // Default to 5% take profit
};