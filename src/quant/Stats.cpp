#include "Stats.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace quant
{
    double mean(const Vector& values)
    {
        if (values.empty())
            return 0.0;
        double sum = 0;
        for (double v : values)
            sum += v;
        return sum / values.size();
    }

    double variance(const Vector& values)
    {
        if (values.size() < 2)
            return 0.0;
        double m = mean(values);
        double sum = 0;
        for (double v : values)
            sum += (v - m) * (v - m);
        return sum / (values.size() - 1);
    }

    double stdev(const Vector& values)
    {
        return std::sqrt(variance(values));
    }

    Vector logReturns(const Vector& prices)
    {
        Vector out;
        for (size_t i = 1; i < prices.size(); i++)
            out.push_back(std::log(prices[i] / prices[i - 1]));
        return out;
    }

    Vector simpleReturns(const Vector& prices)
    {
        Vector out;
        for (size_t i = 1; i < prices.size(); i++)
            out.push_back(prices[i] / prices[i - 1] - 1.0);
        return out;
    }

    static double covariance(const Vector& a, const Vector& b)
    {
        size_t n = std::min(a.size(), b.size());
        if (n < 2)
            return 0.0;
        double ma = mean(a), mb = mean(b);
        double sum = 0;
        for (size_t i = 0; i < n; i++)
            sum += (a[i] - ma) * (b[i] - mb);
        return sum / (n - 1);
    }

    double correlation(const Vector& a, const Vector& b)
    {
        double sa = stdev(a), sb = stdev(b);
        return (sa > 0 && sb > 0) ? covariance(a, b) / (sa * sb) : 0.0;
    }

    Matrix covarianceMatrix(const Matrix& series)
    {
        size_t k = series.size();
        Matrix cov(k, Vector(k, 0.0));
        for (size_t i = 0; i < k; i++)
            for (size_t j = i; j < k; j++)
                cov[i][j] = cov[j][i] = covariance(series[i], series[j]);
        return cov;
    }

    Matrix correlationFromCovariance(const Matrix& covariance)
    {
        size_t k = covariance.size();
        Matrix corr(k, Vector(k, 0.0));
        for (size_t i = 0; i < k; i++)
            for (size_t j = 0; j < k; j++)
            {
                double d = std::sqrt(covariance[i][i] * covariance[j][j]);
                corr[i][j] = d > 0 ? covariance[i][j] / d : (i == j ? 1.0 : 0.0);
            }
        return corr;
    }

    Matrix cholesky(const Matrix& a)
    {
        size_t n = a.size();
        Matrix l(n, Vector(n, 0.0));
        for (size_t i = 0; i < n; i++)
        {
            for (size_t j = 0; j <= i; j++)
            {
                double sum = a[i][j];
                for (size_t k = 0; k < j; k++)
                    sum -= l[i][k] * l[j][k];

                if (i == j)
                {
                    if (sum <= 0)
                        throw std::runtime_error("Covariance matrix is not positive definite "
                                                 "(duplicate stocks, or too little data?)");
                    l[i][i] = std::sqrt(sum);
                }
                else
                {
                    l[i][j] = sum / l[j][j];
                }
            }
        }
        return l;
    }

    double percentile(Vector values, double q)
    {
        if (values.empty())
            return 0.0;
        std::sort(values.begin(), values.end());
        double position = std::clamp(q, 0.0, 1.0) * (values.size() - 1);
        size_t below = static_cast<size_t>(std::floor(position));
        size_t above = std::min(below + 1, values.size() - 1);
        double fraction = position - below;
        return values[below] + fraction * (values[above] - values[below]);
    }

    Regression ols(const Vector& x, const Vector& y)
    {
        Regression r;
        size_t n = std::min(x.size(), y.size());
        if (n < 2)
            return r;

        double mx = mean(x), my = mean(y);
        double sxx = 0, sxy = 0, syy = 0;
        for (size_t i = 0; i < n; i++)
        {
            sxx += (x[i] - mx) * (x[i] - mx);
            sxy += (x[i] - mx) * (y[i] - my);
            syy += (y[i] - my) * (y[i] - my);
        }
        r.beta = sxx > 0 ? sxy / sxx : 0.0;
        r.alpha = my - r.beta * mx;
        r.rSquared = (sxx > 0 && syy > 0) ? (sxy * sxy) / (sxx * syy) : 0.0;

        r.residuals.resize(n);
        for (size_t i = 0; i < n; i++)
            r.residuals[i] = y[i] - r.alpha - r.beta * x[i];
        return r;
    }

    // Inverse of a small square matrix by Gauss-Jordan elimination with partial pivoting
    static Matrix invert(Matrix a)
    {
        size_t n = a.size();
        Matrix inv(n, Vector(n, 0.0));
        for (size_t i = 0; i < n; i++)
            inv[i][i] = 1.0;

        for (size_t col = 0; col < n; col++)
        {
            size_t pivot = col;
            for (size_t row = col + 1; row < n; row++)
                if (std::fabs(a[row][col]) > std::fabs(a[pivot][col]))
                    pivot = row;
            if (std::fabs(a[pivot][col]) < 1e-14)
                throw std::runtime_error("Regression is singular (a regressor is constant or duplicated)");
            std::swap(a[col], a[pivot]);
            std::swap(inv[col], inv[pivot]);

            double scale = a[col][col];
            for (size_t j = 0; j < n; j++)
            {
                a[col][j] /= scale;
                inv[col][j] /= scale;
            }
            for (size_t row = 0; row < n; row++)
            {
                if (row == col)
                    continue;
                double factor = a[row][col];
                for (size_t j = 0; j < n; j++)
                {
                    a[row][j] -= factor * a[col][j];
                    inv[row][j] -= factor * inv[col][j];
                }
            }
        }
        return inv;
    }

    MultiRegression olsMulti(const Matrix& rows, const Vector& y)
    {
        MultiRegression result;
        size_t n = rows.size();
        if (n == 0)
            return result;
        size_t k = rows[0].size();
        if (n <= k)
            throw std::runtime_error("Regression needs more observations than coefficients");

        Matrix xtx(k, Vector(k, 0.0));
        Vector xty(k, 0.0);
        for (size_t i = 0; i < n; i++)
            for (size_t a = 0; a < k; a++)
            {
                xty[a] += rows[i][a] * y[i];
                for (size_t b = 0; b < k; b++)
                    xtx[a][b] += rows[i][a] * rows[i][b];
            }

        Matrix inv = invert(xtx);
        result.coefficients.assign(k, 0.0);
        for (size_t a = 0; a < k; a++)
            for (size_t b = 0; b < k; b++)
                result.coefficients[a] += inv[a][b] * xty[b];

        double sse = 0;
        for (size_t i = 0; i < n; i++)
        {
            double fitted = 0;
            for (size_t a = 0; a < k; a++)
                fitted += rows[i][a] * result.coefficients[a];
            sse += (y[i] - fitted) * (y[i] - fitted);
        }
        result.residualVariance = sse / (n - k);

        result.standardErrors.assign(k, 0.0);
        for (size_t a = 0; a < k; a++)
            result.standardErrors[a] = std::sqrt(std::max(0.0, result.residualVariance * inv[a][a]));
        return result;
    }

    double adfStatistic(const Vector& series, int lags)
    {
        // diff[t] = series[t] - series[t-1]
        // Regress diff[t] on: 1, series[t-1], diff[t-1], ..., diff[t-lags]
        size_t n = series.size();
        size_t first = static_cast<size_t>(lags) + 1;
        if (n < first + 10)
            return 0.0;

        Matrix rows;
        Vector target;
        for (size_t t = first; t < n; t++)
        {
            Vector row = {1.0, series[t - 1]};
            for (int lag = 1; lag <= lags; lag++)
                row.push_back(series[t - lag] - series[t - lag - 1]);
            rows.push_back(row);
            target.push_back(series[t] - series[t - 1]);
        }

        MultiRegression fit = olsMulti(rows, target);
        return fit.standardErrors[1] > 0 ? fit.coefficients[1] / fit.standardErrors[1] : 0.0;
    }

    double halfLife(const Vector& spread)
    {
        // spread[t] - spread[t-1] = c + gamma * spread[t-1]  ->  deviations shrink by (1 + gamma) a day
        if (spread.size() < 3)
            return std::numeric_limits<double>::infinity();
        Vector lagged(spread.begin(), spread.end() - 1);
        Vector change;
        for (size_t t = 1; t < spread.size(); t++)
            change.push_back(spread[t] - spread[t - 1]);

        double gamma = ols(lagged, change).beta;
        if (gamma >= 0 || gamma <= -1)
            return gamma >= 0 ? std::numeric_limits<double>::infinity() : 0.0;
        return -std::log(2.0) / std::log(1.0 + gamma);
    }

    std::string CointegrationResult::verdict() const
    {
        if (adf < EG_CRITICAL_1) return "yes (99%)";
        if (adf < EG_CRITICAL_5) return "yes (95%)";
        if (adf < EG_CRITICAL_10) return "weak (90%)";
        return "no";
    }

    CointegrationResult engleGranger(const Vector& x, const Vector& y, int lags)
    {
        CointegrationResult result;
        Regression fit = ols(x, y);
        result.alpha = fit.alpha;
        result.beta = fit.beta;
        result.adf = adfStatistic(fit.residuals, lags);
        result.halfLifeDays = halfLife(fit.residuals);
        result.days = static_cast<int>(fit.residuals.size());
        return result;
    }

    Performance performance(const Vector& dailyReturns, double periodsPerYear)
    {
        Performance p;
        p.days = static_cast<int>(dailyReturns.size());
        if (dailyReturns.empty())
            return p;

        double equity = 1.0, peak = 1.0;
        for (double r : dailyReturns)
        {
            equity *= 1.0 + r;
            peak = std::max(peak, equity);
            p.maxDrawdown = std::max(p.maxDrawdown, 1.0 - equity / peak);
        }
        p.totalReturn = equity - 1.0;
        p.cagr = equity > 0 ? std::pow(equity, periodsPerYear / p.days) - 1.0 : -1.0;

        double sd = stdev(dailyReturns);
        p.annualVolatility = sd * std::sqrt(periodsPerYear);
        p.sharpe = sd > 0 ? mean(dailyReturns) / sd * std::sqrt(periodsPerYear) : 0.0;
        return p;
    }
}
