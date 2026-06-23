#include "platter_feedback.hpp"

#include <cmath>

namespace platter_feedback {

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
