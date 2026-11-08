#pragma once

#include <cmath>
#include <ctime>
#include <random>
#include <string>
#include <vector>
#include "quant/Stats.hpp"

// Synthetic daily data for the quant tests (seeded, so every run is identical)
namespace synthetic
{
    // Weekdays starting Monday 2018-01-01
    inline std::vector<std::string> weekdays(size_t count)
    {
        std::vector<std::string> dates;
        std::time_t t = 1514764800;   // 2018-01-01 00:00 UTC
        while (dates.size() < count)
        {
            std::tm day = *std::gmtime(&t);
            if (day.tm_wday != 0 && day.tm_wday != 6)
            {
                char text[16];
                std::strftime(text, sizeof(text), "%Y-%m-%d", &day);
                dates.push_back(text);
            }
            t += 24 * 60 * 60;
        }
        return dates;
    }

    // Prices of a random walk in log space
    inline quant::Vector randomWalk(size_t days, double dailyVol, unsigned seed, double start = 100.0, double drift = 0.0)
    {
        quant::NormalRandom normal(seed);
        quant::Vector prices = {start};
        for (size_t i = 1; i < days; i++)
            prices.push_back(prices.back() * std::exp(drift + dailyVol * normal()));
        return prices;
    }

    // Mean-reverting AR(1) series: s[t] = phi * s[t-1] + noise
    inline quant::Vector ar1(size_t days, double phi, double noiseVol, unsigned seed)
    {
        quant::NormalRandom normal(seed);
        quant::Vector s = {0.0};
        for (size_t i = 1; i < days; i++)
            s.push_back(phi * s.back() + noiseVol * normal());
        return s;
    }
}
