#pragma once

#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * RegulatoryFees
 *
 * What Alpaca passes on from the regulators when you sell shares (short sales included). It charges no
 * commission on stocks, so these are the only fees.
 *
 *   SEC fee (Section 31)   $ per $1 million sold: $20.60 since 4 April 2026 (sec.gov fee rate advisory 2026-2)
 *   FINRA TAF              $ per share sold, capped per trade: $0.000166, at most $8.30 (the 2025 rates)
 *
 * Alpaca adds the fees up through the day and charges them at the end of it, so they aren't rounded per trade
 * here. In the config (config/general.json, screen.json, optimisation.json) every rate can be changed:
 *
 *   "regulatory_fees": {"sec_fee_per_million": 20.60, "finra_taf_per_share": 0.000166, "finra_taf_max": 8.30}
 *
 * Alpaca's CAT fee (per share, on buys and sells) isn't included: Alpaca doesn't publish a rate, and it is far
 * smaller than these.
 */
struct RegulatoryFees
{
    static constexpr double SEC_FEE_PER_MILLION = 20.60;
    static constexpr double FINRA_TAF_PER_SHARE = 0.000166;
    static constexpr double FINRA_TAF_MAX = 8.30;
    static constexpr double ONE_MILLION = 1e6;

    double secFeePerMillion = 0;
    double finraTafPerShare = 0;
    double finraTafMax = 0;

    // No fees (the simulated broker's default, so its own tests aren't affected)
    RegulatoryFees() = default;

    // Today's rates, changed by any key in settings
    explicit RegulatoryFees(const json& settings)
        : secFeePerMillion(settings.value("sec_fee_per_million", SEC_FEE_PER_MILLION)),
          finraTafPerShare(settings.value("finra_taf_per_share", FINRA_TAF_PER_SHARE)),
          finraTafMax(settings.value("finra_taf_max", FINRA_TAF_MAX))
    {
    }

    // The config's "regulatory_fees" block, or today's rates if it has none
    static RegulatoryFees fromConfig(const json& config)
    {
        return RegulatoryFees(config.value("regulatory_fees", json::object()));
    }

    // Fees on selling `shares` at `price`
    double onSale(double shares, double price) const
    {
        double secFee = shares * price * secFeePerMillion / ONE_MILLION;
        double finraTaf = std::min(shares * finraTafPerShare, finraTafMax);
        return secFee + finraTaf;
    }
};
