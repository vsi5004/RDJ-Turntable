#include "backlight_controller.hpp"

namespace hmi {

void BacklightController::reset(uint32_t now_ms)
{
    last_activity_ms_ = now_ms;
    last_fade_ms_ = now_ms;
    level_ = config_.bright_level;
    duty_ = level_to_duty(level_);
}

void BacklightController::note_activity(uint32_t now_ms)
{
    last_activity_ms_ = now_ms;
}

BacklightUpdate BacklightController::tick(uint32_t now_ms)
{
    BacklightUpdate update{duty_, false};
    if (static_cast<uint32_t>(now_ms - last_fade_ms_) < config_.fade_interval_ms)
        return update;
    last_fade_ms_ = now_ms;

    const bool inactive = static_cast<uint32_t>(now_ms - last_activity_ms_)
        > config_.dim_after_ms;
    const float target = inactive ? config_.dim_level : config_.bright_level;
    if (level_ == target) return update;

    if (level_ < target) {
        level_ += config_.fade_in_step;
        if (level_ > target) level_ = target;
    } else {
        level_ -= config_.fade_out_step;
        if (level_ < target) level_ = target;
    }

    const uint8_t next_duty = level_to_duty(level_);
    if (next_duty != duty_) {
        duty_ = next_duty;
        update.duty = duty_;
        update.changed = true;
    }
    return update;
}

uint8_t BacklightController::level_to_duty(float level) const
{
    if (level < config_.dim_level) level = config_.dim_level;
    if (level > config_.bright_level) level = config_.bright_level;
    const float squared = level * level;
    if (squared >= 255.0f) return 255;
    return static_cast<uint8_t>(squared);
}

}  // namespace hmi
