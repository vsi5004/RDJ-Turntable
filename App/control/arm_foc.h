/**
 * arm_foc.h - tonearm-carriage gimbal-motor FOC over TIM3 + a second SimpleFOC Mini (DRV8313).
 *
 * Direct mirror of the platter FOC (see foc.h) for the SECOND motor, differing only in:
 *   - PWM timer:   TIM3 CH1/2/3 = PC6/PC7/PB0, edge-aligned ~20 kHz (GP timer, no RCR -> one
 *                  update/period in up-counting mode). Control step runs in the TIM3 update ISR.
 *   - Driver pins: EN = ARM_EN (PC4), nFAULT = ARM_FAULT (PC5).
 *   - Encoder:     the loop is closed by the AS5048A (SPI2), NOT the platter's MT6826S. The
 *                  AS5048A is 14-bit (16384 counts/rev). See as5048a.cpp (now ISR-hardened).
 *
 * Voltage-mode FOC, hand-rolled inverse Park/Clarke + sinusoidal modulation, no current sensing.
 * Bring-up order matches the platter: OpenLoop (count pole-pairs) -> Align (zero offset + direction)
 * -> ClosedLoop torque/velocity. Alignment is held in RAM only for now (no NVM slot yet).
 *
 * NOTE: while a closed-loop mode runs, the TIM3 ISR owns SPI2 - the app must NOT call
 * as5048a::read() from the main loop; use mechanical_angle() / electrical_angle() instead.
 */
#pragma once
#include "main.h" /* HAL + ARM_EN / ARM_FAULT pin labels */
#include <cstdint>

namespace arm_foc {

/* --- Bench-tunable configuration (mutable so we can poke them live during bring-up) --- */
inline float    supply_voltage = 24.0f; /* VM rail; sets the duty<->volt scale */
inline float    voltage_limit  = 1.0f;  /* Uq ceiling, V. START SMALL, ramp up once verified */
inline uint8_t  pole_pairs     = 7;     /* TODO confirm empirically in bring-up (OpenLoop) */
inline float    align_voltage  = 1.5f;  /* Ud used to park the rotor during alignment */
inline float    torque_voltage = 1.0f;  /* Uq for closed-loop torque mode (START SMALL) */

/* Velocity PI loop (same structure as the platter; tune on the bench). */
inline float    vel_target = 0.0f;   /* target MOTOR velocity, rad/s */
inline float    vel_kp     = 0.05f;  /* proportional gain, V per (rad/s) */
inline float    vel_ki     = 0.50f;  /* integral gain, V per (rad/s * s) */
inline float    uq_limit   = 8.0f;   /* clamp on the loop's q-axis voltage output, V */
inline float    vel_lpf_a  = 0.1f;   /* velocity low-pass EMA coeff; 1.0 = off */

/* Position PD loop - the carriage is a SERVO (position mode), not a spindle. Uq = pos_kp*err -
 * pos_kd*velocity, clamped at uq_limit. Targets the unwrapped continuous angle, so multi-turn jogs
 * (e.g. across a lead screw) hold correctly. Tune on the bench. */
inline float    pos_kp        = 2.0f;   /* V per rad of position error */
inline float    pos_kd        = 0.05f;  /* V per (rad/s) of velocity (damping) */
inline float    pos_tol_rad   = 0.05f;  /* "settled" window, rad (~2.9 deg) */

/* Filled by the alignment routine; consumed by closed-loop commutation. */
inline float    zero_elec_offset = 0.0f; /* mechanical angle (rad) where theta_elec == 0 */
inline int8_t   direction        = 1;    /* +1 / -1: sign of mechanical vs electrical motion */
inline bool     alignment_valid  = false;/* set only after a live alignment (no NVM slot yet) */

enum class Mode : uint8_t { Idle, OpenLoop, Align, ClosedLoop, Velocity, Position };

/* Build the sine LUT and start TIM3 PWM. Driver is left DISABLED (ARM_EN low, motor coasting). */
void init();

/* DRV8313 enable line. disable() cuts drive instantly (motor coasts). */
void enable();
void disable();

/* True while the DRV8313 is reporting a fault (ARM_FAULT asserted low). */
bool faulted();

/* Spin open-loop at a target ELECTRICAL velocity (rad/s). Enables the driver. */
void set_open_loop_electrical_velocity(float elec_rad_s);

/* Spin open-loop at a target MECHANICAL velocity (rad/s) - needs pole_pairs known. */
void set_open_loop_velocity(float mech_rad_s);

/* Park the rotor by holding a fixed electrical angle (Ud = align_voltage). Enables the driver. */
void hold_electrical_angle(float theta_el);

/* Map a measured MECHANICAL angle (rad) to electrical angle (rad), using the alignment results. */
float mech_to_electrical(float theta_mech);

/* Closed-loop torque commutation: commute on the measured AS5048A angle, drive a constant Uq. */
void set_closed_loop_torque(float uq);

/* Closed-loop VELOCITY: PI loop drives Uq to hold target_mech_rad_s. Run alignment first. */
void set_closed_loop_velocity(float target_mech_rad_s);

/* Closed-loop POSITION: hold an ABSOLUTE unwrapped mechanical angle (rad). Run alignment first. */
void set_target_position(float target_mech_rad);

/* Relative move: target the current position + delta (the diagnostic "jog a set distance"). Holds
 * the new position when reached. Run alignment first. */
void jog(float delta_mech_rad);

/* Unwrapped continuous mechanical position (rad), its commanded target, and whether the loop has
 * settled within pos_tol_rad. */
float position();
float position_target();
bool  position_settled();

/* Latest AS5048A mechanical angle (rad) cached by the closed-loop ISR. */
float mechanical_angle();

/* Latest estimated MECHANICAL velocity (rad/s) and applied q-axis voltage (V). */
float velocity();
float applied_uq();

/* Stop commanding and disable the driver. */
void stop();

/* Current state (for RTT readout). angle_el in radians [0, 2pi). */
Mode  mode();
float electrical_angle();

/* Called once per PWM period from the TIM3 update ISR (dispatched in foc.cpp). */
void on_update();

/* Low-level: apply dq-frame voltages at an electrical angle. */
void set_phase_voltage(float Ud, float Uq, float theta_el);

}  // namespace arm_foc
