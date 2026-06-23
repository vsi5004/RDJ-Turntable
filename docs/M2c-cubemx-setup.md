# Milestone 2c — CubeMX additions: TIM1 3-PWM + driver EN/FAULT (platter motor FOC)

Brings up **TIM1** as a center-aligned 3-PWM generator (~20 kHz) to drive the **SimpleFOC Mini
(DRV8313)** in `BLDCDriver3PWM` mode, plus two GPIOs for the driver's **EN** (enable) and
**nFAULT** (fault read-back). No current sensing yet — voltage-mode FOC. The FOC control step
runs in the **TIM1 update ISR** (one interrupt per PWM period).

Open `RDJ-Turntable.ioc`, apply the following, **Generate Code**, then reply "generated".

## 1. TIM1 — 3× PWM, center-aligned, ~20 kHz

**Timers → TIM1**:
- *Clock Source*: **Internal Clock**
- *Channel1*: **PWM Generation CH1**
- *Channel2*: **PWM Generation CH2**
- *Channel3*: **PWM Generation CH3**
  (leave CH1N/CH2N/CH3N **off** — the DRV8313 has internal dead-time, so we drive 3 single
  high-side PWMs, not complementary pairs)

Pins assigned automatically: **PA8 = TIM1_CH1, PA9 = TIM1_CH2, PA10 = TIM1_CH3** — all free.

**Parameter Settings → Counter Settings**:
- *Prescaler (PSC)*: **0**
- *Counter Mode*: **Center Aligned mode 3** (update on both over/underflow; RCR halves it)
- *Counter Period (ARR)*: **4199**
  → f_pwm = 168 MHz / (2 × (4199+1)) = **20.0 kHz** (TIM1 clock is 168 MHz: APB2 = 84 MHz, ×2)
- *Internal Clock Division*: **No Division**
- *Repetition Counter (RCR)*: **1**
  → update event fires **once per PWM period** (20 kHz) instead of twice. This is the FOC ISR rate.
- *auto-reload preload*: **Enable**

**Parameter Settings → PWM Generation Channel 1/2/3** (same for all three):
- *Mode*: **PWM mode 1**
- *Pulse*: **0**
- *Output compare preload*: **Enable**
- *Fast Mode*: **Disable**
- *CH Polarity*: **High**

**NVIC Settings (TIM1 tab)**: tick **TIM1 update interrupt and TIM10 global interrupt**. Leave
its preemption priority at **0** — lowest number = highest priority, which is exactly what this
FOC control ISR wants (it sits above SysTick at 15). `on_update()` does only math + register
writes, so being above the HAL tick is safe.

> We do *not* enable the Break/dead-time config (DRV8313 handles dead-time internally).

## 2. Driver control GPIO

| Pin | Mode | User Label | GPIO settings |
|-----|------|-----------|---------------|
| PB13 | GPIO_Output | `PLAT_EN`    | Output level **Low** (driver disabled at reset), push-pull, no pull |
| PB14 | GPIO_Input  | `PLAT_FAULT` | **Pull-up** (DRV8313 nFAULT is open-drain, active-low) |

Both pins are free. EN must read 3.3 V-logic high to enable the DRV8313 (it is on the board).

## 3. Generate

**GENERATE CODE**. `App/`, `CMakeLists.txt`, and the `app_init/app_run` hooks survive.
After generating you'll have:
- `Core/Src/tim.c` / `Core/Inc/tim.h` defining `htim1` and `MX_TIM1_Init()`
- `main.h` defining `PLAT_EN_Pin`/`PLAT_EN_GPIO_Port` and `PLAT_FAULT_Pin`/`PLAT_FAULT_GPIO_Port`
- `TIM1_UP_TIM10_IRQHandler()` in `stm32f4xx_it.c` calling `HAL_TIM_IRQHandler(&htim1)`

## Wiring (SimpleFOC Mini / DRV8313 → board)

| SimpleFOC Mini | Board | Notes |
|----------------|-------|-------|
| IN1 | PA8  | TIM1_CH1 |
| IN2 | PA9  | TIM1_CH2 |
| IN3 | PA10 | TIM1_CH3 |
| EN  | PB13 (PLAT_EN) | drive high to enable |
| nFAULT | PB14 (PLAT_FAULT) | open-drain, board pull-up |
| GND | GND | **common ground with the board is required** |
| VM  | 24 V | motor supply (8–35 V OK) |
| OUT1/2/3 | motor phases A/B/C | |

> **Bench safety:** connect the **bare motor only** (no belt/platter) for first spin. The FOC
> module boots with the driver **disabled** and `voltage_limit` at a small fraction of the bus.

## After "generated"

I'll add `App/control/foc.{h,cpp}` (already pre-staged) to `CMakeLists.txt`, wire `foc::init()`
into `app_init()` and an open-loop spin command into `app_run()`, and route the TIM1 update ISR
to `foc::on_update()` via `HAL_TIM_PeriodElapsedCallback`. Then you flash.

**M2c-1 expected behaviour (open-loop):** the bare motor spins smoothly at the commanded
velocity. RTT streams the MT6826S mechanical angle so we can **count pole-pairs** (mechanical
revs per commanded electrical rev) before moving to encoder-aligned closed loop (M2c-2/3).
