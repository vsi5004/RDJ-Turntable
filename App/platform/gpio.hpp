/**
 * gpio.hpp - typed, polarity-aware GPIO pin wrappers over the STM32 HAL.
 *
 * Methods inline to a single HAL_GPIO_* call, so this is zero-cost in the hot path.
 * (The objects can't be constexpr because the HAL's GPIOx macros cast an int to a pointer,
 * which isn't a constant expression - so they get trivial startup init as `const` globals.)
 * Encoding the active level once means call sites read intent (on()/off()/active()) instead
 * of GPIO_PIN_SET/RESET + remembering polarity.
 */
#pragma once
#include "main.h" /* GPIO_TypeDef, HAL_GPIO_* */
#include <cstdint>

namespace gpio {

class OutputPin {
public:
    constexpr OutputPin(GPIO_TypeDef* port, uint16_t pin, bool active_high = true)
        : port_(port), pin_(pin), active_high_(active_high) {}

    void write(bool active) const
    {
        HAL_GPIO_WritePin(port_, pin_,
                          (active == active_high_) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
    void on()     const { write(true); }
    void off()    const { write(false); }
    void toggle() const { HAL_GPIO_TogglePin(port_, pin_); }

private:
    GPIO_TypeDef* port_;
    uint16_t      pin_;
    bool          active_high_;
};

class InputPin {
public:
    constexpr InputPin(GPIO_TypeDef* port, uint16_t pin, bool active_high = true)
        : port_(port), pin_(pin), active_high_(active_high) {}

    /* True when the pin is at its asserted level. */
    bool active() const
    {
        return (HAL_GPIO_ReadPin(port_, pin_) == GPIO_PIN_SET) == active_high_;
    }

private:
    GPIO_TypeDef* port_;
    uint16_t      pin_;
    bool          active_high_;
};

}  // namespace gpio
