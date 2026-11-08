#include "MonteCarloRisk.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

namespace
{
    // Fewer shared days than this and the correlations aren't worth trusting
    const size_t MIN_DAYS = 60;

    // Confidence levels for value at risk: the worst 1 period in 20, and in 100
    const double CONFIDENCE_95 = 0.95;
    const double CONFIDENCE_99 = 0.99;
}

namespace quant
{
    Vector
    MonteCarloRisk::simulate(const Vector& dailyMean, const Matrix& dailyCovariance, const Vector& weights,
                             int horizonDays, int simulations, unsigned seed)
    {
        size_t k = dailyMean.size();

        // Daily log returns add up, so over h days: mean * h and covariance * h
        Vector horizonMean(k);
        Matrix horizonCovariance = dailyCovariance;
        for (size_t i = 0; i < k; i++)
        {
            horizonMean[i] = dailyMean[i] * horizonDays;
            for (size_t j = 0; j < k; j++)
                horizonCovariance[i][j] *= horizonDays;
        }
        Matrix l = cholesky(horizonCovariance);

        NormalRandom normal(seed);   // same numbers on every platform

        Vector results(simulations);
        Vector z(k);
        for (int s = 0; s < simulations; s++)
        {
            for (size_t i = 0; i < k; i++)
                z[i] = normal();

            double portfolioReturn = 0;
            for (size_t i = 0; i < k; i++)
            {
                // Correlated log return of stock i: mean + row i of L times z
                double logReturn = horizonMean[i];
                for (size_t j = 0; j <= i; j++)
                    logReturn += l[i][j] * z[j];
                portfolioReturn += weights[i] * (std::exp(logReturn) - 1.0);
            }
            results[s] = portfolioReturn;
        }
        return results;
    }

    TailRisk
    MonteCarloRisk::tailRisk(Vector returns, double confidence)
    {
        TailRisk risk;
        if (returns.empty())
            return risk;
        std::sort(returns.begin(), returns.end());

        // VaR: the loss at the (1 - confidence) quantile. ES: the average of everything at or beyond it.
        double cutoff = percentile(returns, 1.0 - confidence);
        risk.var = std::max(0.0, -cutoff);

        double sum = 0;
        int count = 0;
        for (double r : returns)
        {
            if (r > cutoff)
                break;
            sum += r;
            count++;
        }
        risk.es = count > 0 ? std::max(0.0, -sum / count) : risk.var;
        return risk;
    }

    RiskResult
    MonteCarloRisk::run(const PriceTable& prices, RiskSettings settings)
    {
        size_t k = prices.symbols.size();
        if (k == 0)
            throw std::runtime_error("No stocks given");
        if (prices.days() < MIN_DAYS)
            throw std::runtime_error("Need at least " + std::to_string(MIN_DAYS) + " days of prices that all stocks share");

        RiskResult result;
        result.symbols = prices.symbols;

        // Weights: equal by default, otherwise normalised to sum to 1
        Vector w = settings.weights.empty() ? Vector(k, 1.0) : settings.weights;
        if (w.size() != k)
            throw std::runtime_error("Need one weight per stock");
        double total = 0;
        for (double v : w)
            total += v;
        if (total <= 0)
            throw std::runtime_error("Weights must add up to more than 0");
        for (double& v : w)
            v /= total;
        result.weights = w;

        // 1. Estimate: mean, volatility and covariance of daily log returns
        Matrix returns;
        Vector dailyMean;
        for (size_t i = 0; i < k; i++)
        {
            returns.push_back(logReturns(prices[i]));
            dailyMean.push_back(mean(returns.back()));
        }
        Matrix cov = covarianceMatrix(returns);
        result.correlation = correlationFromCovariance(cov);

        Matrix uncorrelatedCov(k, Vector(k, 0.0));
        double weightedVolatility = 0;
        for (size_t i = 0; i < k; i++)
        {
            result.annualReturn.push_back(dailyMean[i] * TRADING_DAYS);
            result.annualVolatility.push_back(std::sqrt(cov[i][i] * TRADING_DAYS));
            uncorrelatedCov[i][i] = cov[i][i];
            weightedVolatility += w[i] * result.annualVolatility.back();
        }

        double portfolioVariance = 0, uncorrelatedVariance = 0;
        for (size_t i = 0; i < k; i++)
            for (size_t j = 0; j < k; j++)
            {
                portfolioVariance += w[i] * w[j] * cov[i][j];
                uncorrelatedVariance += w[i] * w[j] * uncorrelatedCov[i][j];
            }
        result.portfolioAnnualVolatility = std::sqrt(portfolioVariance * TRADING_DAYS);
        result.portfolioAnnualVolatilityUncorrelated = std::sqrt(uncorrelatedVariance * TRADING_DAYS);
        result.diversificationRatio = result.portfolioAnnualVolatility > 0
                                    ? weightedVolatility / result.portfolioAnnualVolatility : 0.0;

        // 2. Simulate (the uncorrelated run uses the same random numbers, so only correlation differs)
        Vector simulated = simulate(dailyMean, cov, w, settings.horizonDays, settings.simulations, settings.seed);
        Vector uncorrelated = simulate(dailyMean, uncorrelatedCov, w, settings.horizonDays,
                                       settings.simulations, settings.seed);

        // 3. Read the risk numbers off the distribution
        result.meanReturn = mean(simulated);
        result.probabilityOfLoss = static_cast<double>(std::count_if(simulated.begin(), simulated.end(),
                                       [](double r) { return r < 0; })) / simulated.size();
        result.at95 = tailRisk(simulated, CONFIDENCE_95);
        result.at99 = tailRisk(simulated, CONFIDENCE_99);
        result.uncorrelated95 = tailRisk(uncorrelated, CONFIDENCE_95);
        result.uncorrelated99 = tailRisk(uncorrelated, CONFIDENCE_99);
        std::sort(simulated.begin(), simulated.end());
        result.simulatedReturns = simulated;

        // 4. Reality check: how often did real horizon-length windows lose more than the model's VaR?
        size_t h = static_cast<size_t>(settings.horizonDays);
        int breaches95 = 0, breaches99 = 0;
        result.worstHistorical = 0;
        for (size_t t = 0; t + h < prices.days(); t++)
        {
            double r = 0;
            for (size_t i = 0; i < k; i++)
                r += w[i] * (prices[i][t + h] / prices[i][t] - 1.0);
            result.historicalWindows++;
            if (r < -result.at95.var) breaches95++;
            if (r < -result.at99.var) breaches99++;
            if (r < result.worstHistorical)
            {
                result.worstHistorical = r;
                result.worstHistoricalStart = prices.dates[t];
            }
        }
        if (result.historicalWindows > 0)
        {
            result.historicalBreachRate95 = static_cast<double>(breaches95) / result.historicalWindows;
            result.historicalBreachRate99 = static_cast<double>(breaches99) / result.historicalWindows;
        }
        return result;
    }
}
