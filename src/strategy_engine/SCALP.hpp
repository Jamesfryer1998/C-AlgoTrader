#pragma once

#include "StrategyBase.hpp"
#include "../util/Format.hpp"
#include "../util/Log.hpp"
#include "../util/MarketTime.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

/**
 * SCALP - VWAP fade scalping on 1-minute bars
 *
 * VWAP is the average price of the day weighted by volume, from the 09:30 open. Around it we draw bands
 * k standard deviations wide, where the standard deviation is how far (volume-weighted) today's prices have
 * been from the VWAP:
 *
 *   VWAP  = sum(volume x price) / sum(volume)
 *   sigma = sqrt(sum(volume x price^2) / sum(volume) - VWAP^2)
 *   bands = VWAP -/+ entry_threshold x sigma
 *
 *   Flat and the close is below the lower band -> BUY  (stretched down, expect a snap back to VWAP)
 *   Flat and the close is above the upper band -> SELL (stretched up)
 *   Long and the close is back to VWAP - exit_threshold x sigma -> SELL to close (target reached)
 *   Short and the close is back to VWAP + exit_threshold x sigma -> BUY to close
 *   Any trade still open after max_hold_bars bars -> close it (scalps are meant to be quick)
 *   From 15:55 New York time -> close any open trade (always flat overnight)
 *
 * New trades only between 09:30 + warmup_minutes (so the VWAP has settled) and 15:45. Bars outside
 * regular hours (pre-market, after-hours) are ignored. Each entry carries a tight stop_loss / take_profit
 * (in %, e.g. 0.3) that the broker manages.
 *
 * The strategy follows the real position the engine reports: if the broker's stop loss closed the trade,
 * it knows it's flat and doesn't send an exit. Trade SCALP on its own for a stock (not alongside other
 * strategies on the same ticker), so that position is its own.
 */
class SCALP : public StrategyBase {
    public:
        static constexpr const char* NAME = "SCALP";
        static constexpr int LAST_ENTRY_MINUTE = 15 * 60 + 45;   // 15:45: no new trades after this
        static constexpr int FLAT_MINUTE = 15 * 60 + 55;         // 15:55: close whatever is open
        static constexpr int MAX_WARMUP_MINUTES = 300;           // warmup_minutes beyond this leaves no time to trade

        SCALP(StrategyAttribute strategyAttribute) : StrategyBase(strategyAttribute) {}

        // A whole session, so the VWAP can be built from the open after a mid-day start
        int barsNeeded() const override { return MarketTime::MINUTES_PER_SESSION + 1; }

        void execute() override
        {
            if (!marketData || marketData->getData().empty())
                return;
            const MarketCondition& bar = currentBar();
            int minute = MarketTime::minuteOfDay(bar.DateTime);
            if (minute < MarketTime::OPEN_MINUTE || minute >= MarketTime::CLOSE_MINUTE)
                return;   // pre-market / after-hours: not part of the session

            bool newBar = bar.DateTime != lastTime;
            updateVwap();
            if (!newBar)
                return;
            lastClose = bar.Close;

            // The broker's stop loss / take profit may have closed the trade: follow the real position
            if (state != 0 && positionKnown() && (currentPosition() == 0 || (currentPosition() > 0) != (state > 0)))
            {
                Log::out() << "SCALP " << bar.Ticker << " trade closed at the broker (stop loss / take profit)" << std::endl;
                state = 0;
            }

            double vwap = this->vwap(), sigma = this->sigma();
            if (state != 0)
            {
                barsHeld++;
                std::string why;
                if (state > 0 && bar.Close >= vwap - _strategyAttribute.exit_threshold * sigma) why = "back to VWAP";
                else if (state < 0 && bar.Close <= vwap + _strategyAttribute.exit_threshold * sigma) why = "back to VWAP";
                else if (barsHeld >= _strategyAttribute.max_hold_bars) why = "time limit";
                else if (minute >= FLAT_MINUTE) why = "end of day";
                if (!why.empty())
                {
                    signal(state > 0 ? OrderType::SELL : OrderType::BUY);
                    Log::out() << "SCALP " << order.getTypeAsString() << " to close (" << why << ") after "
                               << barsHeld << " bar(s)" << std::endl;
                    state = 0;
                }
                return;
            }

            // Flat: look for a stretch, inside trading hours, once the VWAP has had time to settle
            if (minute < MarketTime::OPEN_MINUTE + _strategyAttribute.warmup_minutes || minute >= LAST_ENTRY_MINUTE || sigma <= 0)
                return;
            double band = _strategyAttribute.entry_threshold * sigma;
            if (bar.Close < vwap - band)       { signal(OrderType::BUY);  state = +1; }
            else if (bar.Close > vwap + band)  { signal(OrderType::SELL); state = -1; }
            else
                return;
            barsHeld = 0;
            Log::out() << "SCALP " << order.getTypeAsString() << " | " << std::fixed << std::setprecision(2)
                       << bar.Close << " vs VWAP " << vwap << " +/- " << band << std::endl;
        }

        double vwap() const { return weight > 0 ? weightedPrice / weight : 0.0; }
        double sigma() const
        {
            if (weight <= 0)
                return 0.0;
            double mean = vwap();
            return std::sqrt(std::max(0.0, weightedSquare / weight - mean * mean));
        }
        int getState() const { return state; }
        int getBarsHeld() const { return barsHeld; }

        // VWAP / bands while flat, the trade and its target while in one, or why it's waiting
        std::string status() const override
        {
            if (weight <= 0)
                return "";
            std::ostringstream out;
            double vwap = this->vwap(), sigma = this->sigma();
            out << std::fixed << std::setprecision(2) << "SCALP   VWAP " << vwap << " | ";
            if (state > 0)
                out << "long " << barsHeld << "/" << _strategyAttribute.max_hold_bars << " bars, C: " << lastClose
                    << " | target >= " << vwap - _strategyAttribute.exit_threshold * sigma;
            else if (state < 0)
                out << "short " << barsHeld << "/" << _strategyAttribute.max_hold_bars << " bars, C: " << lastClose
                    << " | target <= " << vwap + _strategyAttribute.exit_threshold * sigma;
            else
            {
                double band = _strategyAttribute.entry_threshold * sigma;
                out << "L: " << vwap - band << " | C: " << lastClose << " | U: " << vwap + band
                    << "   BUY below L, SELL above U";
                int minute = MarketTime::minuteOfDay(lastTime);
                int first = MarketTime::OPEN_MINUTE + _strategyAttribute.warmup_minutes;
                if (minute < first || minute >= LAST_ENTRY_MINUTE)
                    out << "  (no new trades now: " << MarketTime::clock(first) << "-" << MarketTime::clock(LAST_ENTRY_MINUTE) << ")";
            }
            return out.str();
        }

        DecisionLevels decisionLevels() const override
        {
            DecisionLevels levels;
            levels.label = NAME;
            if (weight <= 0)
                return levels;
            levels.ready = true;
            double vwap = this->vwap(), sigma = this->sigma();
            levels.current = Format::number(lastClose);
            std::string atVwap = "VWAP " + Format::number(vwap);
            if (state > 0)
            {
                levels.upper = "exit " + Format::number(vwap - _strategyAttribute.exit_threshold * sigma);
                levels.state = "long " + std::to_string(barsHeld) + "/" + std::to_string(_strategyAttribute.max_hold_bars) + " bars, " + atVwap;
            }
            else if (state < 0)
            {
                levels.lower = "exit " + Format::number(vwap + _strategyAttribute.exit_threshold * sigma);
                levels.state = "short " + std::to_string(barsHeld) + "/" + std::to_string(_strategyAttribute.max_hold_bars) + " bars, " + atVwap;
            }
            else
            {
                double band = _strategyAttribute.entry_threshold * sigma;
                levels.lower = Format::number(vwap - band);
                levels.upper = Format::number(vwap + band);
                int minute = MarketTime::minuteOfDay(lastTime), first = MarketTime::OPEN_MINUTE + _strategyAttribute.warmup_minutes;
                levels.state = "flat, " + atVwap;
                if (minute < first || minute >= LAST_ENTRY_MINUTE)
                    levels.state += " (entries " + MarketTime::clock(first) + "-" + MarketTime::clock(LAST_ENTRY_MINUTE) + ")";
            }
            return levels;
        }

        void validate() override
        {
            if (_strategyAttribute.entry_threshold <= 0)
                throw std::runtime_error("SCALP entry_threshold must be > 0");
            if (_strategyAttribute.exit_threshold < 0 || _strategyAttribute.exit_threshold >= _strategyAttribute.entry_threshold)
                throw std::runtime_error("SCALP exit_threshold must be >= 0 and below entry_threshold");
            if (_strategyAttribute.max_hold_bars <= 0)
                throw std::runtime_error("SCALP max_hold_bars must be > 0");
            if (_strategyAttribute.warmup_minutes < 0 || _strategyAttribute.warmup_minutes > MAX_WARMUP_MINUTES)
                throw std::runtime_error("SCALP warmup_minutes must be between 0 and " + std::to_string(MAX_WARMUP_MINUTES));
            if (_strategyAttribute.stop_loss <= 0 || _strategyAttribute.take_profit <= 0)
                throw std::runtime_error("SCALP needs a stop_loss and take_profit (in %, e.g. 0.3 and 1)");
        }

    private:
        // Keep today's VWAP sums up to date: add the newest bar, or rebuild from the open if the day changed
        // or bars were skipped (e.g. the first call after a back-fill)
        void updateVwap()
        {
            const auto& bars = marketData->getData();
            const MarketCondition& newest = bars.back();
            std::string day = MarketTime::datePart(newest.DateTime);
            if (newest.DateTime == lastTime)
                return;

            bool next = day == sessionDay && bars.size() >= 2 && bars[bars.size() - 2].DateTime == lastTime;
            if (!next)
            {
                if (day != sessionDay)
                    state = 0, barsHeld = 0;   // a new day: no trade carries over
                sessionDay = day;
                weight = weightedPrice = weightedSquare = 0;
                for (const auto& b : bars)
                    if (b.DateTime.compare(0, MarketTime::DATE_LENGTH, day) == 0)
                        add(b);
            }
            else
                add(newest);
            lastTime = newest.DateTime;
        }

        void add(const MarketCondition& b)
        {
            int minute = MarketTime::minuteOfDay(b.DateTime);
            if (minute < MarketTime::OPEN_MINUTE || minute >= MarketTime::CLOSE_MINUTE)
                return;
            double w = b.Volume > 0 ? static_cast<double>(b.Volume) : 1.0;   // no volume (e.g. test data): equal weights
            weight += w;
            weightedPrice += w * b.Close;
            weightedSquare += w * b.Close * b.Close;
        }

        int state = 0;        // +1 long, -1 short, 0 flat
        int barsHeld = 0;
        double lastClose = 0;
        std::string sessionDay, lastTime;
        double weight = 0, weightedPrice = 0, weightedSquare = 0;
};
