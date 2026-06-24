#include "platter_feedback.hpp"

#include <algorithm>
#include <cmath>

namespace platter_feedback {

namespace {

uint32_t magnitude(int32_t value)
{
    if (value >= 0) return static_cast<uint32_t>(value);
    return static_cast<uint32_t>(-(static_cast<int64_t>(value)));
}

}  // namespace

void AbiRpmEstimator::on_sample(const AbiTimerSample& sample)
{
    last_sample_ticks_ = sample.timestamp_ticks;

    if (have_anchor_) {
        const int32_t count_delta = static_cast<int32_t>(sample.position_counts
                                                         - anchor_.position_counts);
        const uint32_t tick_delta = sample.timestamp_ticks - anchor_.timestamp_ticks;
        if (magnitude(count_delta) >= config_.minimum_count_delta && tick_delta != 0
            && config_.counts_per_revolution != 0 && config_.timer_frequency_hz != 0) {
            const float revolutions = static_cast<float>(count_delta)
                / static_cast<float>(config_.counts_per_revolution);
            const float minutes = static_cast<float>(tick_delta)
                / (static_cast<float>(config_.timer_frequency_hz) * 60.0f);
            const float instantaneous = revolutions / minutes;
            if (!valid_)
                rpm_ = instantaneous;
            else
                rpm_ += config_.filter_alpha * (instantaneous - rpm_);
            valid_ = true;
            anchor_ = sample;
        }
    } else {
        anchor_ = sample;
        have_anchor_ = true;
    }

    if (sample.index_sequence != last_index_sequence_) {
        if (have_index_) {
            const uint32_t tick_delta = sample.index_timestamp_ticks - last_index_ticks_;
            const uint32_t revolutions = sample.index_sequence - last_index_sequence_;
            if (tick_delta != 0 && revolutions != 0 && config_.timer_frequency_hz != 0) {
                index_rpm_ = static_cast<float>(revolutions)
                    * static_cast<float>(config_.timer_frequency_hz) * 60.0f
                    / static_cast<float>(tick_delta);
                index_valid_ = true;
            }
        }
        last_index_sequence_ = sample.index_sequence;
        last_index_ticks_ = sample.index_timestamp_ticks;
        have_index_ = true;
    }
}

void AbiRpmEstimator::update(uint32_t now_ticks)
{
    if (!have_anchor_ || static_cast<uint32_t>(now_ticks - last_sample_ticks_)
            > config_.timeout_ticks)
        valid_ = false;
    if (!have_index_ || static_cast<uint32_t>(now_ticks - last_index_ticks_)
            > config_.timer_frequency_hz * 3u)
        index_valid_ = false;
}

void AbiRpmEstimator::reset()
{
    anchor_ = {};
    last_sample_ticks_ = 0;
    last_index_sequence_ = 0;
    last_index_ticks_ = 0;
    rpm_ = 0.0f;
    index_rpm_ = 0.0f;
    have_anchor_ = false;
    have_index_ = false;
    valid_ = false;
    index_valid_ = false;
}

void SpeedTrace::update(uint32_t now_ms, float target_rpm, float measured_rpm,
                        bool measurement_valid)
{
    if (!measurement_valid || target_rpm <= 0.0f) {
        timing_ = false;
        return;
    }
    if (timing_ && static_cast<uint32_t>(now_ms - last_sample_ms_) < config_.sample_period_ms)
        return;

    const float millirpm = (measured_rpm - target_rpm) * 1000.0f;
    const float bounded = std::max(-32768.0f, std::min(32767.0f, millirpm));
    samples_[next_] = static_cast<int16_t>(std::lround(bounded));
    next_ = (next_ + 1u) % capacity;
    if (size_ < capacity) ++size_;
    last_sample_ms_ = now_ms;
    timing_ = true;
}

void SpeedTrace::reset()
{
    for (int16_t& sample : samples_) sample = 0;
    last_sample_ms_ = 0;
    next_ = 0;
    size_ = 0;
    timing_ = false;
}

int16_t SpeedTrace::deviation_millirpm(std::size_t chronological_index) const
{
    if (chronological_index >= size_) return 0;
    const std::size_t oldest = size_ == capacity ? next_ : 0;
    return samples_[(oldest + chronological_index) % capacity];
}

void RpmEstimator::on_edge(uint32_t timestamp_us)
{
    if (have_edge_) {
        const uint32_t period_us = timestamp_us - last_edge_us_;
        if (period_us != 0 && config_.edges_per_revolution != 0) {
            const float instantaneous = 60000000.0f
                / (static_cast<float>(period_us) * config_.edges_per_revolution);
            if (!valid_)
                rpm_ = instantaneous;
            else
                rpm_ += config_.edge_filter_alpha * (instantaneous - rpm_);
            valid_ = true;
        }
    }
    last_edge_us_ = timestamp_us;
    have_edge_ = true;
}

void RpmEstimator::on_index(uint32_t timestamp_us)
{
    if (have_index_) {
        const uint32_t period_us = timestamp_us - last_index_us_;
        if (period_us != 0) {
            index_rpm_ = 60000000.0f / static_cast<float>(period_us);
            index_valid_ = true;
        }
    }
    last_index_us_ = timestamp_us;
    have_index_ = true;
}

void RpmEstimator::update(uint32_t now_us)
{
    if (!have_edge_ || static_cast<uint32_t>(now_us - last_edge_us_) > config_.edge_timeout_us)
        valid_ = false;
    if (!have_index_ || static_cast<uint32_t>(now_us - last_index_us_) > 3000000u)
        index_valid_ = false;
}

void RpmEstimator::reset()
{
    last_edge_us_ = 0;
    last_index_us_ = 0;
    rpm_ = 0.0f;
    index_rpm_ = 0.0f;
    have_edge_ = false;
    have_index_ = false;
    valid_ = false;
    index_valid_ = false;
}

void SpeedLockDetector::set_target(float target_rpm)
{
    target_rpm_ = target_rpm;
    reset();
}

bool SpeedLockDetector::update(uint32_t now_ms, float measured_rpm, bool measurement_valid)
{
    if (!measurement_valid || std::fabs(measured_rpm - target_rpm_) > config_.tolerance_rpm) {
        timing_ = false;
        locked_ = false;
        return false;
    }

    if (!timing_) {
        timing_ = true;
        in_tolerance_since_ms_ = now_ms;
    }
    if (static_cast<uint32_t>(now_ms - in_tolerance_since_ms_) >= config_.stable_time_ms)
        locked_ = true;
    return locked_;
}

void SpeedLockDetector::reset()
{
    in_tolerance_since_ms_ = 0;
    timing_ = false;
    locked_ = false;
}

}  // namespace platter_feedback
