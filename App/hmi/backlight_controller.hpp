#pragma once

#include <cstdint>

namespace hmi {

struct BacklightConfig {
    uint32_t dim_after_ms = 5000;
    uint32_t fade_interval_ms = 10;
    float bright_level = 15.97f;
    float dim_level = 6.71f;
    float fade_out_step = 0.03f;
    float fade_in_step = 0.30f;
};

struct BacklightUpdate {
    uint8_t duty = 255;
    bool changed = false;
};

/**
 * Input-activity-driven perceptual backlight fade.
 *
 * The squared level-to-duty mapping mirrors the proven ScreenKey prototype: fade-out is gentle,
 * wake-up is quick, and operational output is clamped to the readable dim level rather than ever
 * switching the panels off. Duty zero remains a hardware-driver-only boot/init operation.
 */
class BacklightController {
public:
    explicit constexpr BacklightController(BacklightConfig config = {}) : config_(config) {}

    void reset(uint32_t now_ms);
    void note_activity(uint32_t now_ms);
    BacklightUpdate tick(uint32_t now_ms);

    uint8_t duty() const { return duty_; }
    bool dimmed() const { return level_ == config_.dim_level; }

private:
    uint8_t level_to_duty(float level) const;

    BacklightConfig config_;
    uint32_t last_activity_ms_ = 0;
    uint32_t last_fade_ms_ = 0;
    float level_ = 15.97f;
    uint8_t duty_ = 255;
};

}  // namespace hmi
