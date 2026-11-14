#include "DeflatedSharpe.hpp"

#include <cmath>

namespace
{
    const double EULER_GAMMA = 0.5772156649015329;
    const double E = std::exp(1.0);
    const int FEWEST_RETURNS = 3;

    // Acklam's rational approximation of the normal quantile (relative error below 1.2e-9)
    const double A[] = {-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
                        1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00};
    const double B[] = {-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
                        6.680131188771972e+01, -1.328068155288572e+01};
    const double C[] = {-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
                        -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00};
    const double D[] = {7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
                        3.754408661907416e+00};
    const double LOW_TAIL = 0.02425;
}

namespace quant
{
    double normalCdf(double x)
    {
        return 0.5 * std::erfc(-x / std::sqrt(2.0));
    }

    double normalQuantile(double p)
    {
        if (p <= 0)
            return -INFINITY;
        if (p >= 1)
            return INFINITY;
        if (p < LOW_TAIL)
        {
            double q = std::sqrt(-2 * std::log(p));
            return (((((C[0] * q + C[1]) * q + C[2]) * q + C[3]) * q + C[4]) * q + C[5])
                 / ((((D[0] * q + D[1]) * q + D[2]) * q + D[3]) * q + 1);
        }
        if (p > 1 - LOW_TAIL)
            return -normalQuantile(1 - p);
        double q = p - 0.5, r = q * q;
        return (((((A[0] * r + A[1]) * r + A[2]) * r + A[3]) * r + A[4]) * r + A[5]) * q
             / (((((B[0] * r + B[1]) * r + B[2]) * r + B[3]) * r + B[4]) * r + 1);
    }

    double skewness(const Vector& values)
    {
        if (values.size() < FEWEST_RETURNS)
            return 0;
        double average = mean(values), m2 = 0, m3 = 0;
        for (double value : values)
        {
            double d = value - average;
            m2 += d * d;
            m3 += d * d * d;
        }
        m2 /= values.size();
        m3 /= values.size();
        return m2 > 0 ? m3 / std::pow(m2, 1.5) : 0;
    }

    double kurtosis(const Vector& values)
    {
        if (values.size() < FEWEST_RETURNS)
            return 3;
        double average = mean(values), m2 = 0, m4 = 0;
        for (double value : values)
        {
            double d = value - average;
            m2 += d * d;
            m4 += d * d * d * d;
        }
        m2 /= values.size();
        m4 /= values.size();
        return m2 > 0 ? m4 / (m2 * m2) : 3;
    }

    double dailySharpe(const Vector& returns)
    {
        if (returns.size() < 2)
            return 0;
        double deviation = stdev(returns);
        return deviation > 0 ? mean(returns) / deviation : 0;
    }

    double expectedMaxSharpe(double sharpeVariance, int trials)
    {
        if (trials < 2 || sharpeVariance <= 0)
            return 0;
        return std::sqrt(sharpeVariance) * ((1 - EULER_GAMMA) * normalQuantile(1 - 1.0 / trials)
                                            + EULER_GAMMA * normalQuantile(1 - 1.0 / (trials * E)));
    }

    double deflatedSharpe(const Vector& returns, const Vector& trialSharpes)
    {
        if (returns.size() < FEWEST_RETURNS)
            return 0;
        double sharpe = dailySharpe(returns);
        double variance = trialSharpes.size() >= 2 ? quant::variance(trialSharpes) : 0;
        double benchmark = expectedMaxSharpe(variance, static_cast<int>(trialSharpes.size()));
        double spread = 1 - skewness(returns) * sharpe + (kurtosis(returns) - 1) / 4 * sharpe * sharpe;
        if (spread <= 0)
            return 0;
        return normalCdf((sharpe - benchmark) * std::sqrt(returns.size() - 1.0) / std::sqrt(spread));
    }
}
