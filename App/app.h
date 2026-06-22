/**
 * app.h — application entry points, called from CubeMX-generated main().
 *
 *   app_init()  -> once, after HAL/clock/peripheral init (USER CODE BEGIN 2)
 *   app_run()   -> once per main-loop iteration         (USER CODE BEGIN 3)
 *
 * This is the seam between generated HAL init and our hand-written firmware.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void app_init(void);
void app_run(void);

#ifdef __cplusplus
}
#endif
