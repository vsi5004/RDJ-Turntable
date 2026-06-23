/**
 * app.cpp - application top level.
 *
 * M2c platter-motor bring-up (SimpleFOC Mini on TIM1):
 *   KEY0 (Play)  - toggle slow open-loop spin. While spinning we stream the commanded electrical
 *                  angle vs the MT6826S mechanical angle (M2c-1 pole-pair count = seconds per
 *                  mechanical rev at 1 elec rev/s; found 7).
 *   KEY1 (Speed) - run one-shot encoder alignment (M2c-2): park the rotor, solve zero-offset +
 *                  direction for closed-loop commutation.
 *   KEY2 (Menu)  - toggle closed-loop VELOCITY control (M2c-3b): the TIM1 ISR reads the
 *                  MT6826S, commutes, and a PI loop holds vel_target. Run KEY1 align first.
 * Heartbeat LED + tonearm-encoder readout continue as before. Bare motor on the bench first.
 */
#include "app.h"
#include "board.h"
#include "trace.h"
#include "button.hpp"
#include "hmi/screens.h"
#include "control/mt6826s.h"
#include "control/as5048a.h"
#include "control/foc.h"

namespace {

/* Open-loop electrical rate for bring-up: 1 electrical rev/s -> easy pole-pair timing. */
constexpr float kOpenLoopElecVel = 6.28318531f; /* 2*pi rad/s */

/* Belt drive: platter 33.333 RPM via a 90 mm platter pulley driven by a 24 mm motor pulley.
 * Belt linear speed is shared -> motor RPM = platter RPM * (90/24) = 125 RPM = 13.09 rad/s. */
constexpr float kPlatterRpm     = 33.3333f;
constexpr float kBeltRatio      = 90.0f / 24.0f;                       /* platter:motor pulley dia */
constexpr float kMotorTargetVel = kPlatterRpm * kBeltRatio * (2.0f * 3.14159265f / 60.0f);

/* KEY0 (PE8, active-low) doubles as the motor Play/stop toggle (screens also reads it). */
gpio::Button play_key{ gpio::InputPin{ KEY0_GPIO_Port, KEY0_Pin, /*active_high=*/false } };
/* KEY1 runs the one-shot M2c-2 encoder alignment. */
gpio::Button align_key{ gpio::InputPin{ KEY1_GPIO_Port, KEY1_Pin, /*active_high=*/false } };
/* KEY2 toggles M2c-3 closed-loop torque mode. */
gpio::Button loop_key{ gpio::InputPin{ KEY2_GPIO_Port, KEY2_Pin, /*active_high=*/false } };

bool spinning = false; /* open-loop spin (KEY0) */
bool closed   = false; /* closed-loop torque (KEY2) */

constexpr float kPi = 3.14159265f;

/* MT6826S 15-bit count -> mechanical angle in radians [0, 2pi). Bench/idle use only — the
 * closed-loop ISR owns SPI1, so callers must not use this while foc is in ClosedLoop mode. */
float read_mech_rad()
{
    mt6826s::Reading r;
    mt6826s::read(r);
    return static_cast<float>(r.angle) * (2.0f * kPi / 32768.0f);
}

/* M2c-2: park the rotor at two electrical angles, read the encoder at each, and solve for the
 * zero-offset + direction sign used by closed-loop commutation. Blocks ~2 s (rotor held still). */
void run_alignment()
{
    TRACE("\n>>> ALIGN: parking rotor (hold still)...\n");

    foc::hold_electrical_angle(0.0f);
    HAL_Delay(900);                     /* let the rotor settle into the detent */
    float m0 = read_mech_rad();

    foc::hold_electrical_angle(kPi / 3.0f); /* +60 deg electrical step */
    HAL_Delay(900);
    float m1 = read_mech_rad();

    foc::stop();

    /* signed shortest mechanical move for the +60 deg electrical step */
    float dmech = m1 - m0;
    if (dmech >  kPi) dmech -= 2.0f * kPi;
    if (dmech < -kPi) dmech += 2.0f * kPi;

    foc::direction        = (dmech >= 0.0f) ? 1 : -1;
    foc::zero_elec_offset = m0;

    uint32_t off_deg = static_cast<uint32_t>(m0 * (180.0f / kPi));
    int32_t  dm_deg  = static_cast<int32_t>(dmech * (180.0f / kPi));
    TRACE(">>> ALIGN done: offset=%u deg  dir=%c  (mech step %ld deg for +60 elec)\n",
          static_cast<unsigned>(off_deg), foc::direction > 0 ? '+' : '-',
          static_cast<long>(dm_deg));
}

}  // namespace

void app_init(void)
{
    trace_init();
    led.off();
    TRACE("\n=== RDJ-Turntable M2c-1 (platter open-loop FOC) ===\n");
    TRACE("SYSCLK = %u Hz\n", static_cast<unsigned>(HAL_RCC_GetSysClockFreq()));
    screens::init();
    foc::init(); /* TIM1 PWM running; driver DISABLED until KEY0 */
    TRACE("FOC ready. Press KEY0 to start open-loop spin (voltage_limit small, bare motor!).\n");
}

void app_run(void)
{
    static uint32_t last_blink, last_enc;
    uint32_t now = HAL_GetTick();

    /* KEY0 edge: toggle the open-loop spin (not while in closed loop). */
    if (play_key.fell() && !closed) {
        spinning = !spinning;
        if (spinning) {
            foc::set_open_loop_electrical_velocity(kOpenLoopElecVel);
            TRACE("\n>>> SPIN ON  (elec ~1 rev/s). Time one mechanical rev = pole_pairs.\n");
        } else {
            foc::stop();
            TRACE("\n>>> SPIN OFF (driver disabled).\n");
        }
    }

    /* KEY1 edge: run encoder alignment (only when motor idle). */
    if (align_key.fell() && !spinning && !closed) {
        run_alignment();
    }

    /* KEY2 edge: toggle closed-loop velocity (not while open-loop spinning). */
    if (loop_key.fell() && !spinning) {
        closed = !closed;
        if (closed) {
            foc::set_closed_loop_velocity(kMotorTargetVel); /* 33.333 RPM platter via belt ratio */
            uint32_t tgt_x10 = static_cast<uint32_t>(kMotorTargetVel * 10.0f);
            TRACE("\n>>> CLOSED-LOOP VELOCITY ON  target=%u.%u rad/s (33.3 RPM platter). ISR owns SPI1.\n",
                  static_cast<unsigned>(tgt_x10 / 10), static_cast<unsigned>(tgt_x10 % 10));
        } else {
            foc::stop();
            TRACE("\n>>> CLOSED-LOOP OFF (driver disabled).\n");
        }
    }

    if (now - last_blink >= 250) { /* ~2 Hz heartbeat */
        last_blink = now;
        led.toggle();
    }

    /* Faster readout while the motor runs so the angle doesn't alias. */
    uint32_t enc_period = (spinning || closed) ? 40 : 200; /* 25 Hz running, else 5 Hz */
    if (now - last_enc >= enc_period) {
        last_enc = now;
        constexpr float kRadToDeg = 180.0f / 3.14159265f;

        if (closed) {
            /* ISR owns SPI1: read the cached state, never mt6826s::read() here. */
            uint32_t mdeg    = static_cast<uint32_t>(foc::mechanical_angle() * kRadToDeg);
            int32_t  vel_x10 = static_cast<int32_t>(foc::velocity() * 10.0f);  /* rad/s x10 */
            int32_t  uq_mv   = static_cast<int32_t>(foc::applied_uq() * 1000.0f); /* mV */
            int32_t  tgt_x10 = static_cast<int32_t>(foc::vel_target * 10.0f);
            TRACE("LOOP  vel=%ld.%ld/%ld.%ld rad/s  Uq=%ldmV  mech=%3u deg  FAULT=%u\n",
                  static_cast<long>(vel_x10 / 10), static_cast<long>(vel_x10 < 0 ? -vel_x10 % 10 : vel_x10 % 10),
                  static_cast<long>(tgt_x10 / 10), static_cast<long>(tgt_x10 % 10),
                  static_cast<long>(uq_mv), static_cast<unsigned>(mdeg),
                  static_cast<unsigned>(foc::faulted() ? 1u : 0u));
        } else if (spinning) {
            mt6826s::Reading r;
            mt6826s::read(r);
            uint32_t mdeg_x10 = static_cast<uint32_t>(r.angle) * 3600u / 32768u;
            uint32_t edeg = static_cast<uint32_t>(foc::electrical_angle() * kRadToDeg);
            TRACE("SPIN  elec=%3u deg   mech=%u.%u deg  st=0x%02X crc=%s  FAULT=%u\n",
                  static_cast<unsigned>(edeg),
                  static_cast<unsigned>(mdeg_x10 / 10), static_cast<unsigned>(mdeg_x10 % 10),
                  r.status, r.crc_ok ? "OK" : "BAD", static_cast<unsigned>(foc::faulted() ? 1u : 0u));
        } else {
            mt6826s::Reading r;
            mt6826s::read(r);
            uint32_t mdeg_x10 = static_cast<uint32_t>(r.angle) * 3600u / 32768u;
            as5048a::Reading a;
            as5048a::read(a);
            uint32_t adeg_x10 = static_cast<uint32_t>(a.angle) * 3600u / 16384u;
            TRACE("MT6826S angle=%5u  %u.%u deg  st=0x%02X crc=%s   |   "
                  "AS5048A angle=%5u  %u.%u deg  EF=%u parity=%s\n",
                  r.angle, static_cast<unsigned>(mdeg_x10 / 10), static_cast<unsigned>(mdeg_x10 % 10),
                  r.status, r.crc_ok ? "OK" : "BAD",
                  a.angle, static_cast<unsigned>(adeg_x10 / 10), static_cast<unsigned>(adeg_x10 % 10),
                  a.error_flag, a.parity_ok ? "OK" : "BAD");
        }
    }

    screens::tick();
}
