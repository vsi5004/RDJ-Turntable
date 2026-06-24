/**
 * screens.h - array-driven ScreenKey layer (init all panels and render snapshots).
 */
#pragma once

#include "presenter.hpp"

#include <cstdint>

namespace screens {

void init();  /* reset + init all panels, draw first-light pattern */
void set_brightness(uint8_t duty); /* shared backlight PWM: 0=off, 255=full */
void show(const hmi::View& view); /* redraw changed ScreenKeys from an immutable view */
void tick();  /* reserved for incremental/non-blocking rendering */

}  // namespace screens
