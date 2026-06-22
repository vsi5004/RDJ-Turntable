/**
 * app.cpp - application top level.
 *
 * M2a: MT6826S platter-encoder bring-up. Non-blocking loop: LED heartbeat, key polling,
 * and a periodic RTT stream of the absolute angle (rotate the magnet by hand to verify).
 */
#include "app.h"
#include "board.h"
#include "trace.h"
#include "hmi/screens.h"
#include "control/mt6826s.h"

void app_init(void)
{
    trace_init();
    led.off();
    TRACE("\n=== RDJ-Turntable M2a (MT6826S) ===\n");
    TRACE("SYSCLK = %u Hz\n", static_cast<unsigned>(HAL_RCC_GetSysClockFreq()));
    screens::init();
}

void app_run(void)
{
    static uint32_t last_blink, last_enc;
    uint32_t now = HAL_GetTick();

    if (now - last_blink >= 250) { /* ~2 Hz heartbeat */
        last_blink = now;
        led.toggle();
    }

    if (now - last_enc >= 200) { /* 5 Hz encoder readout over RTT */
        last_enc = now;
        mt6826s::Reading r;
        mt6826s::read(r);
        uint32_t deg_x10 = static_cast<uint32_t>(r.angle) * 3600u / 32768u;
        TRACE("MT6826S raw=%02X %02X %02X %02X  angle=%5u  %u.%u deg  st=0x%02X  crc=%s\n",
              r.raw[0], r.raw[1], r.raw[2], r.raw[3],
              r.angle, static_cast<unsigned>(deg_x10 / 10), static_cast<unsigned>(deg_x10 % 10),
              r.status, r.crc_ok ? "OK" : "BAD");
    }

    screens::tick();
}
