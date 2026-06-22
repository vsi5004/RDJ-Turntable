/**
 * screens.h — array-driven ScreenKey layer (init all panels, poll keys, render).
 */
#pragma once

void screens_init(void); /* reset + init all panels, draw first-light pattern */
void screens_tick(void); /* poll keys; (M1) redraw on press */
