# Milestone 4 (hardware) — CubeMX additions: TIM3 3-PWM + driver EN/FAULT (tonearm gimbal motor)

Brings up a **second SimpleFOC Mini (DRV8313)** for the **tonearm carriage gimbal motor**, mirroring
the platter setup ([M2c-cubemx-setup.md](M2c-cubemx-setup.md)) but on **TIM3**. Voltage-mode FOC, 3
single high-side PWMs in `BLDCDriver3PWM` mode, plus **EN** and **nFAULT** GPIOs. The control step
will run in the **TIM3 update ISR** (one interrupt per PWM period) once the M4 control software is
written — this milestone only adds the peripheral/pin hardware.

> **The gimbal loop is closed by the AS5048A** (SPI2, `AS_CS` = PB12) — a different magnetic encoder
> than the platter's MT6826S (SPI1). The platter motor commutates off the MT6826S; the tonearm
> motor commutates/positions off the AS5048A.

This is what's already in `RDJ-Turntable.ioc` (regenerate in CubeMX → **Generate Code**).

## Why TIM3 (not TIM8) and these pins

The other advanced-control timer, TIM8, hard-maps CH3 to **PC8 = SDIO_D0**, and we keep the microSD
bank (**PC8–PC12, PD2**) free for future logging/diagnostics. So the gimbal motor uses **TIM3**, a
general-purpose timer, on pins that avoid that bank:

- `TIM3_CH1` = **PC6**, `TIM3_CH2` = **PC7** — these are SDIO_D6/D7, which a microSD (1- or 4-bit
  only) never uses, so they remain free even when SD is added.
- `TIM3_CH3` = **PB0** — used instead of the SD-critical PC8.

TIM3 lacks the advanced timer's repetition counter, complementary outputs, and break input — but the
DRV8313 uses **none** of those (it does its own dead-time/shoot-through protection; we drive 3 single
PWMs and read its nFAULT GPIO). Running TIM3 **edge-aligned** gives one update/period at the same
**~20 kHz** and the **same ~4200-count duty resolution** as the platter's center-aligned TIM1, so
there is no FOC performance penalty for this driver.

The 32-bit TIM2/TIM5 + PA0/PA1/PA2 stay reserved for the platter ABI encoder (see
[M2c-plan.md](M2c-plan.md)); the TIM3/PC6/PC7/PB0 choice does not touch them.

## 1. TIM3 — 3× PWM, edge-aligned, ~20 kHz

**Timers → TIM3**:
- *Clock Source*: **Internal Clock**
- *Channel1/2/3*: **PWM Generation CH1/CH2/CH3** (leave CHxN off — single high-side PWMs)

Pins assigned: **PC6 = TIM3_CH1, PC7 = TIM3_CH2, PB0 = TIM3_CH3**.

**Parameter Settings → Counter Settings**:
- *Prescaler (PSC)*: **0**
- *Counter Mode*: **Up** (edge-aligned — GP timer has no RCR, so this gives one update/period)
- *Counter Period (ARR)*: **4199**
  → f_pwm = 84 MHz / (4199+1) = **20.0 kHz** (TIM3 clock is 84 MHz on APB1: PCLK1 42 MHz ×2)
- *Internal Clock Division*: **No Division**
- *auto-reload preload*: **Enable**

**Parameter Settings → PWM Generation Channel 1/2/3** (same for all three):
- *Mode*: **PWM mode 1**, *Pulse*: **0**, *Fast Mode*: **Disable**, *CH Polarity*: **High**

**NVIC Settings (TIM3 tab)**: tick **TIM3 global interrupt**, preemption priority **1** — one step
below the platter FOC (TIM1 @ 0), since the tonearm is the less time-critical loop. (No update
interrupt actually fires until the M4 control code calls `__HAL_TIM_ENABLE_IT(&htim3, TIM_IT_UPDATE)`
and starts PWM.)

## 2. Driver control GPIO

| Pin | Mode | User Label | GPIO settings |
|-----|------|-----------|---------------|
| PC4 | GPIO_Output | `ARM_EN`    | Output level **Low** (driver disabled at reset), push-pull, no pull |
| PC5 | GPIO_Input  | `ARM_FAULT` | **Pull-up** (DRV8313 nFAULT is open-drain, active-low) |

PC4/PC5 are spare, non-critical GPIO (ADC pins, and we use no ADC), away from the SD and ABI banks.

## 3. Generated artifacts

After **Generate Code** the `.ioc` produces:
- `Core/Src/tim.c` / `Core/Inc/tim.h`: `htim3` + `MX_TIM3_Init()` (edge-aligned, ARR=4199, 3 PWM),
  MspPostInit putting PB0/PC6/PC7 on **AF2_TIM3**, MspInit enabling the clock + `TIM3_IRQn`
- `main.h`: `ARM_EN_Pin`/`ARM_EN_GPIO_Port` (PC4) and `ARM_FAULT_Pin`/`ARM_FAULT_GPIO_Port` (PC5)
- `gpio.c`: `ARM_EN` driven **low** at boot (driver disabled), `ARM_FAULT` input pull-up
- `main.c`: `MX_TIM3_Init()` call
- `stm32f4xx_it.c`: `TIM3_IRQHandler()` calling `HAL_TIM_IRQHandler(&htim3)`

## Wiring (second SimpleFOC Mini / DRV8313 → board)

| SimpleFOC Mini | Board | Notes |
|----------------|-------|-------|
| IN1 | PC6 | TIM3_CH1 |
| IN2 | PC7 | TIM3_CH2 |
| IN3 | PB0 | TIM3_CH3 |
| EN  | PC4 (ARM_EN) | drive high to enable |
| nFAULT | PC5 (ARM_FAULT) | open-drain, board pull-up |
| GND | GND | **common ground with the board is required** |
| VM  | motor supply | 8–35 V |
| OUT1/2/3 | gimbal motor phases A/B/C | |

> **Bench safety:** the driver boots **disabled** (ARM_EN low). Commutation feedback comes from the
> **AS5048A** on SPI2, which is already up (see [M2b-cubemx-setup.md](M2b-cubemx-setup.md)).

## Driver software (included)

The control software is now in the tree, mirroring the platter FOC:

- **`App/control/arm_foc.{h,cpp}`** — voltage-mode FOC for TIM3 + AS5048A + ARM_EN/ARM_FAULT, with
  the platter's modes (open-loop spin, electrical align, closed-loop torque/velocity) **plus a
  closed-loop POSITION (PD) mode** — the carriage is a servo, not a spindle. `set_target_position()`
  / `jog(delta)` target the unwrapped continuous angle so multi-turn jogs hold correctly. Edge-aligned
  20 kHz; `kCountToRad = 2π/16384` for the AS5048A's 14-bit count.

### Carriage jog diagnostic (target browser)

Diagnostics now open on a **target browser** (per the state-machine doc): KEY1 selects the current
target, KEY2 cycles PLATTER ↔ TONEARM, KEY0 exits; KEY0-hold on a test page backs out to the browser.
Selecting **TONEARM** gives the carriage test page:

| Key | Action |
|---|---|
| KEY0 | **JOG −** (one fixed motor-angle step back) / STOP while moving |
| KEY1 | **AUTO ALIGN** — electrical align; writes the commutation cal to flash (`nvm::Slot::Tonearm`, sector 6) |
| KEY2 | **JOG +** (one step forward) / STOP while moving |

Jog step defaults to **90° of motor angle** (`InteractionConfig::diagnostic_jog_step_rad`, set in
`app.cpp`) — convert to carriage mm once the lead-screw/pulley ratio is known. A jog needs a valid
alignment first (closed-loop position commutates off the AS5048A); the executor rejects it otherwise.
The reached position is **held** (motor stays energized) until the next jog or exit.

The diagnostic executor (`M2cExecutor`) routes by `command.target`: the electrical-align state
machine and the encoder read are **shared** between platter and tonearm (per-motor dispatch), and
alignment is persisted per motor via the slotted NVM (platter → sector 7, tonearm → sector 6).
- **`App/control/as5048a.cpp`** — hardened to a **raw bounded-spin SPI2** read (16-bit frames, no
  `HAL_MAX_DELAY`) so it is safe to call from the TIM3 ISR; `parity_ok` doubles as the transfer-OK
  flag and the ISR holds its last good angle on a bad read (mirrors the MT6826S hardening).
- **`App/control/foc.cpp`** — the single `HAL_TIM_PeriodElapsedCallback` now fans out by instance:
  TIM1 → `foc::on_update()`, TIM3 → `arm_foc::on_update()`.
- **`app.cpp`** — `arm_foc::init()` brings up TIM3 PWM + the control ISR at boot, **driver disabled**
  (ARM_EN low). No command path is wired into the UI/diagnostics yet, so the motor stays coasting.

**Bring-up (same as the platter, M2c):** count pole-pairs with an open-loop spin (default
`pole_pairs = 7` is a guess — confirm it), run electrical align to get the zero offset + direction,
then close the loop. Alignment is held in RAM only for now (no NVM slot yet). The carriage ultimately
wants **position** control, not velocity — that loop is a later addition on top of this driver.

> **SPI2 ownership:** while an `arm_foc` closed-loop mode runs, the TIM3 ISR owns SPI2 — do not call
> `as5048a::read()` from the main loop; use `arm_foc::mechanical_angle()` instead (same contract the
> platter has with SPI1/MT6826S).
