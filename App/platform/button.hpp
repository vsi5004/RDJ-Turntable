/**
 * button.hpp - momentary button on an InputPin, with press-edge detection.
 *
 * Polarity lives in the InputPin, so this stays logical: pressed()/fell() regardless of
 * whether the switch is active-low or active-high.
 */
#pragma once
#include "gpio.hpp"

namespace gpio {

class Button {
public:
    constexpr explicit Button(InputPin pin) : pin_(pin) {}

    bool pressed() const { return pin_.active(); }

    /* True once per press (released -> pressed transition). Call once per poll. */
    bool fell()
    {
        bool now  = pin_.active();
        bool edge = now && !prev_;
        prev_ = now;
        return edge;
    }

private:
    InputPin pin_;
    bool     prev_ = false;
};

}  // namespace gpio
