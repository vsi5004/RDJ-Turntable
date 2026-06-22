/**
 * app.c — Milestone 0: prove the toolchain/debug/clock chain.
 *
 * Verifies, end to end:
 *   - LED on PC13 blinks at ~2 Hz             -> GPIO + 168 MHz clock are alive
 *   - RTT prints boot SYSCLK + a tick counter -> trace path works, clock is correct
 *   - `tick` is a live-watchable variable      -> debugger connection works
 */
#include "app.h"
#include "board.h"
#include "trace.h"

static volatile uint32_t tick; /* volatile: keep it live for the debugger Watch panel */

void app_init(void)
{
    trace_init();
    LED_OFF();
    TRACE("\n=== RDJ-Turntable M0 ===\n");
    TRACE("SYSCLK = %u Hz (expect 168000000)\n",
          (unsigned)HAL_RCC_GetSysClockFreq());
}

void app_run(void)
{
    LED_TOGGLE();
    TRACE("tick %u\n", (unsigned)tick++);
    HAL_Delay(250); /* ~2 Hz blink */
}
