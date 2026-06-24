# Milestone 2c — Platter motor FOC (plan / handoff)

Status: **M2c-1/2/3 working on hardware (bare motor, MT6826S feedback only — no platter/optical
encoder yet).** TIM1 (PA8/9/10, ARR=4199, RCR=1, center-aligned, update IRQ @ prio 0) +
PLAT_EN=PB13 / PLAT_FAULT=PB14. `App/control/foc.{h,cpp}` wired in; ISR via `HAL_TIM_PeriodElapsedCallback`.
- **M2c-1 open-loop (KEY0):** spins; pole_pairs = **7** (51.4 deg mech/elec rev).
- **M2c-2 align (KEY1):** offset=289 deg, dir=+, repeatable; stored in `foc::zero_elec_offset`/`direction`.
- **M2c-3a torque (API only):** smooth commutation confirmed; droops under load (expected, no regulation).
- **M2c-3b velocity PI (KEY2):** ISR commutes + 1 kHz PI holds `vel_target`. Target = 13.09 rad/s
  (125 RPM motor -> 33.333 RPM platter via 90/24 belt ratio). uq_limit=8V. Gains kp=0.05/ki=0.50 UNTUNED.
**Next: confirm velocity loop holds under load at 13.09 rad/s; tune gains (re-tune once platter inertia
added). Then M2c-4 MT6826S auto-cal. Pending hardening: SPI read in ISR uses HAL_MAX_DELAY (move to
DMA/raw poll). Align values not persisted (re-align each boot until M2c-4 EEPROM).**

## Hardware (confirmed)

- **Driver: SimpleFOC Mini** (DRV8313). Input **8–35 V → 24 V rail is in range** (resolves the
  design-log TODO). 5 A max; 3.3 V-logic compatible. Interface = `BLDCDriver3PWM`:
  - **3 PWM inputs** IN1/IN2/IN3 + **EN** (enable, drive high to enable)
  - Optional: **nFAULT** (active-low, read to detect faults), nSLEEP, nRESET
  - DRV8313 has **internal dead-time** → we drive 3 single PWMs (NOT complementary pairs)
- **Motors:** 2808 (or smaller) gimbals, voltage mode. Pole-pairs unknown → measure at open-loop.

## Pin / timer allocation

- **Platter motor (this milestone):** **TIM1** CH1/CH2/CH3 = **PA8/PA9/PA10** (3 PWM) + an **EN**
  GPIO + (optional) **nFAULT** input GPIO. Center-aligned PWM, **~20 kHz**
  (ARR = 168 MHz / (2 × 20 kHz) − 1 = 4199, prescaler 0; TIM1 clk = 168 MHz on APB2).
  FOC update in the **TIM1 update ISR** (high priority).
- **Tonearm motor (M4, later):** **TIM8** CH1/2/3 = **PC6/PC7/PC8** (overlaps unused microSD pins).
- **Platter shaft ABI (provisional; hardware phase):** **TIM2** encoder mode with
  **PA0/TIM2_CH1 = A** and **PA1/TIM2_CH2 = B**; **PA2/EXTI2 = index**. TIM2 is a 32-bit
  counter, so normal polling cannot lose position to a short 16-bit rollover.
- **ABI measurement timebase (provisional; hardware phase):** **TIM5**, free-running at its
  84 MHz APB1 timer clock and consuming no external pin. The capture adapter will publish atomic
  position/time snapshots and the most recent index timestamp. The precise snapshot trigger
  (periodic, selected ABI edges, or both) remains a hardware-phase measurement decision.
- Encoders already up: platter **MT6826S/SPI1**, tonearm **AS5048A/SPI2**.
- **ScreenKey backlight:** **TIM4_CH1/PD12**, 12 kHz PWM (ARR=6999 at the 84 MHz APB1 timer clock).
  This timer is independent of both motor PWM timers and the provisional ABI timers.

This allocation keeps both three-phase PWM timers intact: TIM1 remains dedicated to platter FOC and
TIM8 remains reserved for the tonearm gimbal FOC. PA0, PA1, and PA2 are unassigned in the current
CubeMX project. These ABI assignments are reservations only; do not add TIM2, TIM5, or the GPIOs to
the `.ioc` until the shaft encoder is ready for hardware integration.

## Bring-up order (safe → informative)

1. **M2c-1 Open-loop spin:** ramp an electrical-angle voltage vector at a low voltage limit;
   motor follows open-loop. Verifies PWM/driver/motor wiring; **count pole-pairs** (mechanical
   revs on MT6826S vs commanded electrical revs). **Test the bare motor (no belt/platter) first.**
2. **M2c-2 Encoder align:** hold a known θ_elec, let the rotor settle, read MT6826S → the
   zero-electrical-angle **offset** and the **direction** sign.
3. **M2c-3 Closed-loop FOC:** θ_elec = (θ_mech − offset) × pole_pairs (mod 2π); drive q-axis
   voltage for torque. Hand-rolled math in the TIM1 ISR:
   - inverse Park: Uα = Ud·cosθ − Uq·sinθ ; Uβ = Ud·sinθ + Uq·cosθ
   - inverse Clarke / SVPWM → 3 phase duties around 50%; write TIM1 CCR1/2/3
   - start sinusoidal (inverse Clarke + 0.5 offset); add SVPWM center-shift later for bus usage
4. **M2c-4 MT6826S auto-cal:** ✅ DONE. Must spin **open-loop** at constant speed (closed loop
   injects eccentricity-synced ripple and fails the cal). KEY1 long-hold runs it: ramp open-loop
   to ~180 RPM, AUTOCAL_FREQ=0x5 (100–200 RPM band, matches our 125 RPM operating point), trigger
   0x5E→0x155, poll 0x113[7:6]. Auto-cal self-commits to EEPROM on success (no separate Program
   EEPROM step) → power-cycle. Verified: velocity ripple dropped ~60% (±20%→±7%) at 13 rad/s.
   See `docs/mt6826s-calibration.md` (verified against `docs/datasheets/MT6826S_Rev1.1.pdf`).

## Safety

- Bare motor on the bench first (a wiring error wiggles a free motor, not the platter).
- Start with a **low voltage limit** (e.g. a small fraction of the bus); ramp up once verified.
- Wire **nFAULT** and check it; the EN pin lets us cut drive instantly.

## Conventions reminder (from memory)

C++17 App layer, one namespace per driver, `static constexpr` constants, `enum class`,
GPIO via `gpio::OutputPin/InputPin` (active-level aware). RTT logging via `TRACE(...)` —
**no %f** (print fixed-point). Build: `cmake --preset Debug && cmake --build --preset Debug`;
flash/debug via F5 (cortex-debug → J-Link). New peripherals are added in CubeMX (then a guide
+ pre-staged driver, integrated after "generated").
