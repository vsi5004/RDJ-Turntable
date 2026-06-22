/**
 * trace.h — thin logging shim over SEGGER RTT.
 *
 * RTT streams over the SWD link (no extra pins, no UART). View it with
 * cortex-debug's RTT terminal or `JLinkRTTClient`. Keeping the transport behind
 * TRACE()/trace_init() means we can swap to UART/ITM later without touching call sites.
 */
#pragma once

#include "SEGGER_RTT.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void trace_init(void) { SEGGER_RTT_Init(); }

/* printf-style; channel 0 is the default up-buffer cortex-debug reads. */
#define TRACE(...) SEGGER_RTT_printf(0, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
