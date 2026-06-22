/**
 * app.c — application top level.
 *
 * M1: bring up the ScreenKey displays. The main loop is non-blocking (tick-based
 * LED heartbeat) so key polling stays responsive.
 */
#include "app.h"
#include "board.h"
#include "trace.h"
#include "hmi/screens.h"

void app_init(void)
{
    trace_init();
    LED_OFF();
    TRACE("\n=== RDJ-Turntable M1 ===\n");
    TRACE("SYSCLK = %u Hz (expect 168000000)\n",
          (unsigned)HAL_RCC_GetSysClockFreq());
    screens_init();
}

void app_run(void)
{
    static uint32_t last_blink;
    uint32_t now = HAL_GetTick();

    if (now - last_blink >= 250) { /* ~2 Hz heartbeat */
        last_blink = now;
        LED_TOGGLE();
    }

    screens_tick(); /* poll keys every iteration */
}
