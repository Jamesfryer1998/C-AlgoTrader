#pragma once

#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

/**
 * Small statistics toolkit for the quant projects (Monte Carlo risk, pairs trading, MA crossover).
 * Plain std::vector maths, no external libraries.
 */
namespace quant
{
    using Vector = std::vector<double>;
    using Matrix = std::vector<Vector>;   // Matrix[row][column]

    constexpr double TRADING_DAYS = 252.0;

    // Standard normal random numbers that are the same on every platform for a given seed.
    // (std::normal_distribution is implemented differently by libstdc++ and libc++, so a seeded
    // simulation or test gave different numbers on Linux and macOS.) mt19937_64 is fully specified by the
    // C++ standard; the normals come from it by the Box-Muller method.
    class NormalRandom
    {
        public:
            explicit NormalRandom(std::uint64_t seed) : engine(seed) {}

            double operator()()
            {
                if (haveSpare)
                {
                    haveSpare = false;
                    return spare;
                }
                double u1 = uniform(), u2 = uniform();
                while (u1 <= 0.0)
                    u1 = uniform();
                double radius = std::sqrt(-2.0 * std::log(u1));
                double angle = 2.0 * 3.14159265358979323846 * u2;
                spare = radius * std::sin(angle);
                haveSpare = true;
                return radius * std::cos(angle);
            }

        private:
            double uniform() { return static_cast<double>(engine() >> 11) * 0x1.0p-53; }   // [0, 1)

            std::mt19937_64 engine;
            double spare = 0;
            bool haveSpare = false;
    };

    double mean(const Vector& values);
    double variance(const Vector& values);   // sample variance (divides by n - 1)
    double stdev(const Vector& values);

    // Day-to-day returns: log(p[t] / p[t-1]) and p[t] / p[t-1] - 1. One shorter than the prices.
    Vector logReturns(const Vector& prices);
    Vector simpleReturns(const Vector& prices);

    double correlation(const Vector& a, const Vector& b);

    // series[k] holds the observations of variable k (all the same length)
    Matrix covarianceMatrix(const Matrix& series);
    Matrix correlationFromCovariance(const Matrix& covariance);

    // Lower-triangular L with L * L^T = a. Throws std::runtime_error if a is not positive definite
    // (e.g. two identical stocks, or fewer days of data than stocks).
    Matrix cholesky(const Matrix& a);

    // Value below which a fraction q (0..1) of the values fall, interpolating between neighbours
    double percentile(Vector values, double q);

    // y = alpha + beta * x + residual
    struct Regression
    {
        double alpha = 0;
        double beta = 0;
        double rSquared = 0;
        Vector residuals;
    };
    Regression ols(const Vector& x, const Vector& y);

    // y = rows * coefficients (put a column of 1s in rows for a constant), with standard errors
    struct MultiRegression
    {
        Vector coefficients;
        Vector standardErrors;
        double residualVariance = 0;
    };
    MultiRegression olsMulti(const Matrix& rows, const Vector& y);

    // Augmented Dickey-Fuller t-statistic (with a constant and `lags` lagged differences).
    // The more negative, the stronger the evidence the series is mean reverting rather than a random walk.
    double adfStatistic(const Vector& series, int lags = 1);

    // Engle-Granger critical values for the ADF test on the residuals of a 2-variable regression
    // (MacKinnon). An ADF statistic below these means "cointegrated" at that confidence.
    constexpr double EG_CRITICAL_1 = -3.90;
    constexpr double EG_CRITICAL_5 = -3.34;
    constexpr double EG_CRITICAL_10 = -3.04;

    // Days for a deviation of the spread from its mean to halve (fitted AR(1)).
    // Infinity if the spread doesn't revert.
    double halfLife(const Vector& spread);

    // Engle-Granger cointegration test: regress y on x, then test the residuals (the spread) for a unit root
    struct CointegrationResult
    {
        double alpha = 0;
        double beta = 0;           // hedge ratio: units of x per unit of y (in log prices)
        double adf = 0;
        double halfLifeDays = 0;
        int days = 0;

        bool cointegrated(double critical = EG_CRITICAL_5) const { return adf < critical; }
        std::string verdict() const;   // "yes (99%)", "yes (95%)", "weak (90%)", "no"
    };
    CointegrationResult engleGranger(const Vector& x, const Vector& y, int lags = 1);

    // Summary of a series of daily returns (risk free rate taken as 0)
    struct Performance
    {
        double totalReturn = 0;
        double cagr = 0;
        double annualVolatility = 0;
        double sharpe = 0;
        double maxDrawdown = 0;    // worst peak-to-trough fall, as a positive fraction
        int days = 0;
    };
    // periodsPerYear: how many returns make a year (252 for daily, 252 x 390 for regular-hours minute bars)
    Performance performance(const Vector& dailyReturns, double periodsPerYear = TRADING_DAYS);
}
