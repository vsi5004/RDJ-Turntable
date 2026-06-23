#pragma once

#include <cstdint>

namespace platter_feedback {

struct EstimatorConfig {
    uint32_t edges_per_revolution = 4000;
    float edge_filter_alpha = 0.08f;
    uint32_t edge_timeout_us = 100000;
};

class RpmEstimator {
public:
    explicit constexpr RpmEstimator(EstimatorConfig config = {}) : config_(config) {}

    void on_edge(uint32_t timestamp_us);
    void on_index(uint32_t timestamp_us);
    void update(uint32_t now_us);
    void reset();

    float rpm() const { return rpm_; }
    float index_rpm() const { return index_rpm_; }
    bool valid() const { return valid_; }
    bool index_valid() const { return index_valid_; }

private:
    EstimatorConfig config_;
    uint32_t last_edge_us_ = 0;
    uint32_t last_index_us_ = 0;
    float rpm_ = 0.0f;
    float index_rpm_ = 0.0f;
    bool have_edge_ = false;
    bool have_index_ = false;
    bool valid_ = false;
    bool index_valid_ = false;
};

struct LockConfig {
    float tolerance_rpm = 0.05f;
    uint32_t stable_time_ms = 1000;
};

class SpeedLockDetector {
public:
    explicit constexpr SpeedLockDetector(LockConfig config = {}) : config_(config) {}

    void set_target(float target_rpm);
    bool update(uint32_t now_ms, float measured_rpm, bool measurement_valid);
    bool locked() const { return locked_; }
    void reset();

private:
    LockConfig config_;
    float target_rpm_ = 0.0f;
    uint32_t in_tolerance_since_ms_ = 0;
    bool timing_ = false;
    bool locked_ = false;
};

}  // namespace platter_feedback
