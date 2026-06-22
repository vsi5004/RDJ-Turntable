/**
 * screens.h - array-driven ScreenKey layer (init all panels, poll keys, render).
 */
#pragma once

namespace screens {

void init();  /* reset + init all panels, draw first-light pattern */
void tick();  /* poll keys; (M1) redraw on press */

}  // namespace screens
