#pragma once

#include "gpio.hpp"
#include <cstdint>

namespace gpio {

enum class ButtonEvent : uint8_t { None, Pressed, Tap, Held, Released };

class DebouncedButton {
public:
    constexpr explicit DebouncedButton(InputPin pin, uint32_t debounce_ms = 20)
        : pin_(pin), debounce_ms_(debounce_ms) {}

    ButtonEvent update(uint32_t now_ms, uint32_t hold_ms)
    {
        const bool raw = pin_.active();
        if (raw != raw_state_) {
            raw_state_ = raw;
            raw_changed_ms_ = now_ms;
        }

        if (raw_state_ != stable_state_
            && static_cast<uint32_t>(now_ms - raw_changed_ms_) >= debounce_ms_) {
            stable_state_ = raw_state_;
            if (stable_state_) {
                pressed_ms_ = now_ms;
                hold_emitted_ = false;
                return ButtonEvent::Pressed;
            }
            if (!hold_emitted_) return ButtonEvent::Tap;
            return ButtonEvent::Released;
        }

        if (stable_state_ && !hold_emitted_
            && static_cast<uint32_t>(now_ms - pressed_ms_) >= hold_ms) {
            hold_emitted_ = true;
            return ButtonEvent::Held;
        }
        return ButtonEvent::None;
    }

    bool pressed() const { return stable_state_; }

    uint8_t hold_progress(uint32_t now_ms, uint32_t hold_ms, uint8_t segments = 32) const
    {
        if (!stable_state_ || hold_ms == 0 || segments == 0) return 0;
        const uint32_t elapsed = static_cast<uint32_t>(now_ms - pressed_ms_);
        if (elapsed >= hold_ms) return segments;
        return static_cast<uint8_t>((elapsed * segments) / hold_ms);
    }

private:
    InputPin pin_;
    uint32_t debounce_ms_ = 20;
    uint32_t raw_changed_ms_ = 0;
    uint32_t pressed_ms_ = 0;
    bool raw_state_ = false;
    bool stable_state_ = false;
    bool hold_emitted_ = false;
};

}  // namespace gpio
