#pragma once

#include "Stats.hpp"

/**
 * The deflated Sharpe ratio (Bailey and López de Prado, 2014): how likely a strategy's true Sharpe ratio is
 * above zero, once you allow for having tried several strategies and kept the best, and for returns that
 * aren't normally distributed.
 *
 *   SR0  = the best Sharpe expected from `trials` strategies with no edge at all
 *        = sqrt(variance of the trials' Sharpes) x ((1 - g) x Z(1 - 1/N) + g x Z(1 - 1/(N e)))   g: Euler's constant
 *   DSR  = Phi( (SR - SR0) x sqrt(T - 1) / sqrt(1 - skew x SR + (kurtosis - 1) / 4 x SR^2) )
 *
 * Sharpe ratios here are per period (daily), not annualised. Z is the standard normal's inverse, Phi its
 * cumulative distribution. A DSR of 0.95 or more is the usual bar for "probably real".
 */
namespace quant
{
    double normalCdf(double x);
    double normalQuantile(double p);   // inverse of normalCdf, for 0 < p < 1

    double skewness(const Vector& values);
    double kurtosis(const Vector& values);   // not excess: a normal distribution's is 3

    // The best Sharpe expected by luck from `trials` strategies whose Sharpes vary by sharpeVariance
    double expectedMaxSharpe(double sharpeVariance, int trials);

    // returns: the chosen strategy's daily returns; trialSharpes: every tried strategy's daily Sharpe (itself
    // included). 0 when there's too little to judge.
    double deflatedSharpe(const Vector& returns, const Vector& trialSharpes);

    // Daily (not annualised) Sharpe: mean / standard deviation. 0 if it doesn't vary.
    double dailySharpe(const Vector& returns);
}
