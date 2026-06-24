#pragma once

#include <cstddef>
#include <cstdint>

namespace platter_feedback {

/**
 * Hardware-neutral snapshot of the platter's ABI encoder timers.
 *
 * The eventual STM32 adapter can obtain this from a hardware quadrature counter and a separate
 * free-running timer.  Consumers deliberately know nothing about HAL timer handles or GPIOs.
 * Unsigned subtraction makes both 32-bit fields safe across timer rollover as long as adjacent
 * samples are less than half the counter range apart.
 */
struct AbiTimerSample {
    uint32_t position_counts = 0;
    uint32_t timestamp_ticks = 0;
    uint32_t index_sequence = 0;
    uint32_t index_timestamp_ticks = 0;
};

class IAbiTimerCapture {
public:
    virtual ~IAbiTimerCapture() = default;
    virtual AbiTimerSample capture() const = 0;
};

struct AbiEstimatorConfig {
    uint32_t counts_per_revolution = 4000;
    uint32_t timer_frequency_hz = 84000000;
    uint32_t minimum_count_delta = 32;
    uint32_t timeout_ticks = 8400000;
    float filter_alpha = 0.12f;
};

/**
 * M/T-style speed estimator using encoder count change (M) over precise timer ticks (T).
 *
 * Samples may be produced periodically or on selected encoder edges.  The estimator accumulates
 * samples until minimum_count_delta is reached, avoiding the one-tick/one-edge quantization of a
 * microsecond reciprocal-period estimator while keeping the acquisition policy replaceable.
 */
class AbiRpmEstimator {
public:
    explicit constexpr AbiRpmEstimator(AbiEstimatorConfig config = {}) : config_(config) {}

    void on_sample(const AbiTimerSample& sample);
    void update(uint32_t now_ticks);
    void reset();

    float rpm() const { return rpm_; }
    float index_rpm() const { return index_rpm_; }
    bool valid() const { return valid_; }
    bool index_valid() const { return index_valid_; }

private:
    AbiEstimatorConfig config_;
    AbiTimerSample anchor_{};
    uint32_t last_sample_ticks_ = 0;
    uint32_t last_index_sequence_ = 0;
    uint32_t last_index_ticks_ = 0;
    float rpm_ = 0.0f;
    float index_rpm_ = 0.0f;
    bool have_anchor_ = false;
    bool have_index_ = false;
    bool valid_ = false;
    bool index_valid_ = false;
};

struct SpeedTraceConfig {
    uint32_t sample_period_ms = 100;
};

/** Fixed-capacity speed-error history suitable for the ScreenKey sparkline. */
class SpeedTrace {
public:
    static constexpr std::size_t capacity = 64;

    void update(uint32_t now_ms, float target_rpm, float measured_rpm, bool measurement_valid);
    void reset();

    std::size_t size() const { return size_; }
    int16_t deviation_millirpm(std::size_t chronological_index) const;

private:
    SpeedTraceConfig config_{};
    int16_t samples_[capacity]{};
    uint32_t last_sample_ms_ = 0;
    std::size_t next_ = 0;
    std::size_t size_ = 0;
    bool timing_ = false;
};

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
