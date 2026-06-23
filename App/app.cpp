/**
 * app.cpp - application top level.
 *
 * M2c platter-motor bring-up (SimpleFOC Mini on TIM1):
 *   KEY0 (Play)  - toggle slow open-loop spin. While spinning we stream the commanded electrical
 *                  angle vs the MT6826S mechanical angle (M2c-1 pole-pair count = seconds per
 *                  mechanical rev at 1 elec rev/s; found 7).
 *   KEY1 (Speed) - tap: one-shot encoder alignment (M2c-2). hold >2.5s: MT6826S auto-calibration
 *                  (M2c-4, DESTRUCTIVE EEPROM write — power-cycle after success).
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

/* M2c-4 auto-cal: spin OPEN-LOOP at a steady speed (no feedback loop to inject eccentricity-
 * synced ripple, which fails the cal). ~180 RPM motor -> AUTOCAL_FREQ=0x5 band (100-200 RPM). */
constexpr float   kCalRpm     = 180.0f; /* open-loop motor speed during calibration */
constexpr uint8_t kCalFreq    = 0x5;    /* 100-200 RPM band */
constexpr float   kCalVoltage = 3.0f;   /* open-loop Uq to sustain sync at cal speed (bump if it slips) */

/* KEY0 (PE8, active-low) doubles as the motor Play/stop toggle (screens also reads it). */
gpio::Button play_key{ gpio::InputPin{ KEY0_GPIO_Port, KEY0_Pin, /*active_high=*/false } };
/* KEY1: tap = M2c-2 encoder align, long-hold = M2c-4 auto-calibration (destructive). */
gpio::Button align_key{ gpio::InputPin{ KEY1_GPIO_Port, KEY1_Pin, /*active_high=*/false } };
/* KEY2 toggles M2c-3 closed-loop velocity. */
gpio::Button loop_key{ gpio::InputPin{ KEY2_GPIO_Port, KEY2_Pin, /*active_high=*/false } };

bool spinning = false; /* open-loop spin (KEY0) */
bool closed   = false; /* closed-loop velocity (KEY2) */

constexpr uint32_t kCalHoldMs = 2500; /* KEY1 hold time that triggers auto-calibration */

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

/* M2c-4: MT6826S user auto-calibration. Spins the motor at a steady ~150 RPM (closed loop),
 * triggers the encoder's self-cal, polls status, and reports. DESTRUCTIVE: a successful run
 * commits to the encoder's EEPROM (~1000-cycle endurance) and self-persists — power-cycle after.
 * See docs/mt6826s-calibration.md. Triggered by a deliberate KEY1 long-hold. */
void run_calibration()
{
    TRACE("\n>>> CALIBRATION: MT6826S auto-cal. ONE-TIME (writes EEPROM, ~1000-cycle life).\n");

    mt6826s::set_autocal_freq(kCalFreq);     /* 0x5 = 100-200 RPM band */

    /* Spin OPEN-LOOP: a constant electrical velocity gives uniform shaft rotation with no
     * feedback loop injecting eccentricity-synced ripple. Ramp up so we don't lose sync. */
    float target_elec  = (kCalRpm / 60.0f) * (2.0f * kPi) * foc::pole_pairs;
    float saved_vlimit = foc::voltage_limit;
    foc::voltage_limit = kCalVoltage;
    TRACE(">>> open-loop spin-up to ~%u RPM...\n", static_cast<unsigned>(kCalRpm));
    for (int i = 1; i <= 40; ++i) {          /* ~2 s ramp */
        foc::set_open_loop_electrical_velocity(target_elec * i / 40.0f);
        HAL_Delay(50);
    }
    HAL_Delay(1000);                          /* settle at speed */

    /* Verify actual shaft speed (open-loop: the main loop may read the encoder directly). */
    float a0 = read_mech_rad();
    HAL_Delay(100);
    float a1 = read_mech_rad();
    float d = a1 - a0;
    if (d >  kPi) d -= 2.0f * kPi;
    if (d < -kPi) d += 2.0f * kPi;
    int32_t rpm = static_cast<int32_t>((d / 0.1f) * (60.0f / (2.0f * kPi)));
    TRACE(">>> measured shaft speed ~%ld RPM (need 100-200)\n", static_cast<long>(rpm));

    if (rpm < 80 || rpm > 200) {
        foc::voltage_limit = saved_vlimit;
        foc::stop();
        TRACE(">>> ABORT: not holding the cal band (sync slip?). Adjust kCalVoltage/kCalRpm.\n");
        return;
    }

    mt6826s::start_autocal();                 /* write 0x5E -> 0x155 */
    TRACE(">>> auto-cal running; holding open-loop >18 revs...\n");

    mt6826s::CalStatus st = mt6826s::CalStatus::None;
    for (int i = 0; i < 200; ++i) {           /* up to ~20 s */
        HAL_Delay(100);
        st = mt6826s::autocal_status();
        if (st == mt6826s::CalStatus::Success || st == mt6826s::CalStatus::Failed) break;
        if (i % 10 == 0) TRACE("    ... status=%u (running)\n", static_cast<unsigned>(st));
    }

    foc::voltage_limit = saved_vlimit;
    if (st == mt6826s::CalStatus::Success) {
        foc::stop();                          /* stop driving before further encoder ops */
        TRACE(">>> CAL SUCCESS — coefficients committed to EEPROM.\n");
        TRACE(">>> POWER-CYCLE the board now to load the calibration.\n");
    } else {
        mt6826s::stop_autocal();              /* exit cal mode so a retry is possible */
        foc::stop();
        TRACE(">>> CAL %s (status=%u). Check magnet/air-gap/speed and retry.\n",
              st == mt6826s::CalStatus::Failed ? "FAILED" : "TIMED OUT",
              static_cast<unsigned>(st));
    }
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

    /* KEY1: tap (<2.5s) = align; long-hold (>=2.5s) = auto-calibration. Both motor-idle only. */
    static uint32_t key1_t0 = 0;
    static bool     key1_held = false;
    if (align_key.pressed()) {
        if (key1_t0 == 0) key1_t0 = now;
        else if (!key1_held && (now - key1_t0) >= kCalHoldMs && !spinning && !closed) {
            key1_held = true;        /* fire cal once, on the hold threshold */
            run_calibration();
        }
    } else {
        if (key1_t0 != 0 && !key1_held && !spinning && !closed) {
            run_alignment();         /* short tap on release */
        }
        key1_t0 = 0;
        key1_held = false;
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
