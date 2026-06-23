/**
 * foc.h - platter-motor field-oriented control over TIM1 + SimpleFOC Mini (DRV8313).
 *
 * Voltage-mode FOC (no current sensing yet). Drive interface = BLDCDriver3PWM:
 * three single high-side PWMs on TIM1 CH1/2/3 (PA8/9/10) at ~20 kHz, center-aligned,
 * plus an EN GPIO (PLAT_EN) and a read-only nFAULT input (PLAT_FAULT).
 *
 * The control step (on_update) runs once per PWM period in the TIM1 update ISR. Bring-up
 * order (see docs/M2c-plan.md):
 *   M2c-1 OpenLoop    - ramp the electrical angle at a commanded velocity; count pole-pairs.
 *   M2c-2 (align)     - hold a known angle, read the MT6826S -> zero-offset + direction.
 *   M2c-3 ClosedLoop  - theta_el = (theta_mech - offset) * pole_pairs; drive Uq for torque.
 *
 * Math is hand-rolled (inverse Park + inverse Clarke, sinusoidal modulation). No %f in TRACE.
 */
#pragma once
#include "main.h" /* HAL + PLAT_EN / PLAT_FAULT pin labels */
#include <cstdint>

namespace foc {

/* --- Bench-tunable configuration (mutable so we can poke them live during bring-up) --- */
inline float    supply_voltage = 24.0f; /* VM rail; sets the duty<->volt scale */
inline float    voltage_limit  = 1.0f;  /* Uq ceiling, V. START SMALL, ramp up once verified */
inline uint8_t  pole_pairs     = 7;     /* confirmed empirically in M2c-1 (51.4 deg mech/elec rev) */
inline float    align_voltage  = 1.5f;  /* Ud used to park the rotor during M2c-2 alignment */
inline float    torque_voltage = 1.0f;  /* Uq for M2c-3a closed-loop torque mode (START SMALL) */

/* M2c-3b velocity PI loop (rejects load disturbance -> holds platter speed). Tune on the bench. */
inline float    vel_target = 13.09f; /* target MOTOR velocity, rad/s (app sets from belt ratio) */
inline float    vel_kp     = 0.05f;  /* proportional gain, V per (rad/s) */
inline float    vel_ki     = 0.50f;  /* integral gain, V per (rad/s * s) */
inline float    uq_limit   = 8.0f;   /* clamp on the loop's q-axis voltage output, V (bus 24V) */
inline float    vel_lpf_a  = 0.1f;   /* velocity low-pass EMA coeff (~18 Hz @ 1 kHz); 1.0 = off */

/* Filled by the M2c-2 alignment routine; consumed by closed-loop commutation (M2c-3). */
inline float    zero_elec_offset = 0.0f; /* mechanical angle (rad) where theta_elec == 0 */
inline int8_t   direction        = 1;    /* +1 / -1: sign of mechanical vs electrical motion */

enum class Mode : uint8_t { Idle, OpenLoop, Align, ClosedLoop, Velocity };

/* Build the sine LUT and start TIM1 PWM. Driver is left DISABLED (EN low, coasting). */
void init();

/* DRV8313 enable line. disable() cuts drive instantly (motor coasts). */
void enable();
void disable();

/* True while the DRV8313 is reporting a fault (nFAULT asserted low). */
bool faulted();

/* M2c-1: spin open-loop at a target ELECTRICAL velocity (rad/s). Enables the driver.
 * Pole-pairs unknown at this stage, so we command the electrical rate directly: at 1 elec
 * rev/s, the seconds the shaft takes for one MECHANICAL revolution == pole_pairs. */
void set_open_loop_electrical_velocity(float elec_rad_s);

/* Spin open-loop at a target MECHANICAL velocity (rad/s) — needs pole_pairs known. Enables
 * the driver. Thin wrapper: electrical velocity = mech_rad_s * pole_pairs. */
void set_open_loop_velocity(float mech_rad_s);

/* M2c-2: park the rotor by holding a fixed electrical angle (Ud = align_voltage). Enables the
 * driver. The shaft settles to the magnetic detent at theta_el; read the encoder there. */
void hold_electrical_angle(float theta_el);

/* Map a measured MECHANICAL angle (rad) to electrical angle (rad), using the alignment results.
 * theta_el = wrap( direction * pole_pairs * (theta_mech - zero_elec_offset) ). */
float mech_to_electrical(float theta_mech);

/* M2c-3: closed-loop torque commutation. The TIM1 ISR reads the MT6826S, commutes at the
 * measured angle, and drives a constant q-axis voltage. Run alignment (M2c-2) first.
 * NOTE: while this runs, the ISR owns SPI1 — the app must NOT call mt6826s::read(); use
 * mechanical_angle() / electrical_angle() instead. */
void set_closed_loop_torque(float uq);

/* M2c-3b: closed-loop VELOCITY. ISR commutes on the measured angle and a PI loop drives Uq to
 * hold vel_target. Run alignment (M2c-2) first. Same SPI1-ownership caveat as torque mode. */
void set_closed_loop_velocity(float target_mech_rad_s);

/* Latest MT6826S mechanical angle (rad) cached by the closed-loop ISR. */
float mechanical_angle();

/* Latest estimated MECHANICAL velocity (rad/s) and applied q-axis voltage (V). */
float velocity();
float applied_uq();

/* Stop commanding and disable the driver. */
void stop();

/* Current state (for RTT readout). angle_el in radians [0, 2pi). */
Mode  mode();
float electrical_angle();

/* Called once per PWM period from the TIM1 update ISR. */
void on_update();

/* Low-level: apply dq-frame voltages at an electrical angle (used by open & closed loop). */
void set_phase_voltage(float Ud, float Uq, float theta_el);

}  // namespace foc
