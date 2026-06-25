# Milestone 5 (hardware) — CubeMX additions: ABI encoder, home endstop, CAN, lift servo, arm-angle ADC

This pass completes the board's remaining I/O for the turntable, beyond the two motors already wired
(platter TIM1 + tonearm gimbal TIM3). It adds:

| Function | Pin(s) | Peripheral | Notes |
|---|---|---|---|
| Platter ABI encoder A / B | **PA0 / PA1** | `TIM2_CH1/CH2` (encoder mode) | 32-bit counter; quadrature x4 |
| Platter ABI index (Z) | **PA2** | `EXTI2` (GPIO input) | pull-up, falling edge (open-collector index) |
| Tonearm home endstop | **PE7** | `EXTI7` (GPIO input) | pull-up, falling edge (optical, active-low) |
| CAN bus (TJA transceiver) | **PB8 / PB9** | `CAN1_RX / CAN1_TX` | avoids USB (PA11/PA12); 1 Mbit-capable |
| Tonearm lift servo | **PB15** | `TIM12_CH2` PWM | 50 Hz RC-servo (1–2 ms pulse) |
| Tonearm angle feedback | **PC0** (+ **PC1** reserved) | `ADC1_IN10` (+ `IN11`) | single-ended; PC1 = pseudo-diff leg |

> **The onboard microSD is deliberately NOT used.** On the FK407M1 the socket is wired
> CLK=PD8, CMD=PD3, **DAT0=PC7**, DAT1=PC8, DAT2=PC10, DAT3/CD=PC11 — a scrambled pinout that maps to
> **no** STM32 SDIO or SPI peripheral (so it is bit-bang-only), and its **DAT0 lands on PC7, which is
> the committed tonearm gimbal motor (`TIM3_CH2`)**. `TIM3_CH2` has no free alternate pin (PA7/PB5 are
> SPI1/SPI3), so the socket can't coexist with the motor. If storage is needed later, add an external
> microSD breakout on free pins (e.g. bit-bang SCK=PD8, MOSI=PD3, MISO=PC10, CS=PC11) — never PC7.

This is what's in `RDJ-Turntable.ioc` (regenerate in CubeMX → **Generate Code**).

---

## 1. Platter ABI encoder — TIM2 encoder mode + index EXTI

The platter shaft optical encoder gives high-resolution incremental angle/velocity (for scratch /
phase detection), complementing the MT6826S absolute encoder on the motor shaft. Reserved since
[M2c-plan.md](M2c-plan.md); now wired.

**Timers → TIM2**:
- *Combined Channels*: **Encoder Mode**
- *Mode → Encoder Mode*: **TI1 and TI2** (x4 counting)
- Pins assigned: **PA0 = TIM2_CH1 (A)**, **PA1 = TIM2_CH2 (B)**.

**Parameter Settings → Counter Settings**:
- *Prescaler*: **0**
- *Counter Period (ARR)*: **0xFFFFFFFF** — TIM2 is a **32-bit** counter, so polling never loses
  position to a short rollover (the reason TIM2/TIM5 were reserved for this, not a 16-bit GP timer).
- *Encoder → Input Filter* (both channels): **0xF** (max) — debounce optical edge noise.
- *Polarity*: Rising on both (flip one later if A/B direction reads inverted).

No TIM2 interrupt is needed — the counter **is** the position; the control loop reads `TIM2->CNT`.

**Index (Z) — PA2 as EXTI**, not a timer feature (TIM2 has no clean index-latch on F4):
- `System Core → GPIO → PA2`: **GPIO_EXTI2**, *Pull-up*, *Mode* = **External Interrupt, Falling edge**
  (the optical index is open-collector, idle-high, pulses low once per rev). Label **`PLAT_INDEX`**.
- `NVIC`: enable **EXTI line2 interrupt**. The ISR latches `TIM2->CNT` as the once-per-rev zero.

> If A/B or the index edge read backwards on hardware, flip the channel polarity / EXTI edge in CubeMX
> — both are one-field changes.

---

## 2. Tonearm home endstop — PE7 EXTI

Optical endstop at the **home (retracted) end** of the tonearm linear slide, for homing the carriage.

- `GPIO → PE7`: **GPIO_EXTI7**, *Pull-up*, *Mode* = **External Interrupt, Falling edge** (optical
  output is open-collector / active-low: idle high, blocked = low). Label **`ARM_HOME`**.
- `NVIC`: enable **EXTI line[9:5] interrupt** (shared vector EXTI9_5; PE7 = line 7).

PE7 is isolated from the polled KEY0/1/2 inputs (PE8/9/10) and uses its own EXTI line. (No far-end
limit was added this pass — revisit if a runaway-carriage hard stop is wanted.)

---

## 3. CAN bus — CAN1 on PB8 / PB9 (external TJA transceiver)

- `Connectivity → CAN1`: **Activated**, *Mode* = Master.
- Pins: **PB8 = CAN1_RX**, **PB9 = CAN1_TX**. (Chosen over PA11/PA12, which are the board's USB-C
  D−/D+, and over PD0/PD1 to keep those free. PB8/PB9 are 5 V-tolerant and broken out.)
- *Bit timing*: set for your bus once the transceiver/cable is known (e.g. 500 kbit or 1 Mbit). With
  APB1 = 42 MHz a starting point for **500 kbit** is Prescaler **6**, BS1 **11 TQ**, BS2 **2 TQ**,
  SJW 1 (42 MHz / 6 / 14 = 500 kHz, ~85 % sample point) — verify against your nodes.
- `NVIC`: enable **CAN1 RX0 interrupt** (RX FIFO0) for received-frame handling.

The TJA transceiver is external: MCU TX→TXD, RX←RXD, common GND, 120 Ω termination at the bus ends
(hardware). Nothing else on the MCU side.

---

## 4. Tonearm lift servo — TIM12_CH2 / PB15, 50 Hz

The cue/lift is a standard RC hobby servo (50 Hz frame, 1–2 ms pulse). It needs its **own** timer
base: TIM4 is already the 12 kHz ScreenKey backlight, and a single timer can only run one frequency.

**Timers → TIM12**:
- *Clock Source*: Internal Clock
- *Channel2*: **PWM Generation CH2** → pin **PB15 = TIM12_CH2**. (CH1/PB14 is already `PLAT_FAULT`;
  the alternate CH2 pin PH9 isn't bonded on LQFP100.)
- *Prescaler*: **83** → 84 MHz / 84 = **1 MHz** tick (1 µs). TIM12 is on APB1 (timer clock 84 MHz).
- *Counter Period (ARR)*: **19999** → 1 MHz / 20000 = **50 Hz** frame.
- *PWM Ch2 Pulse*: **1500** (1.5 ms neutral); firmware drives ~1000–2000 µs for the lift travel.
- *auto-reload preload*: Enable.

1 µs resolution over the 1000-count active band is far finer than a hobby servo resolves. No
interrupt — duty is set by writing `TIM12->CCR2`.

---

## 5. Tonearm angle feedback — ADC1 (PC0, + PC1 reserved)

### What it is
The **outer** loop of the carriage servo. The AS5048A (SPI2) closes the *inner* loop (gimbal
commutation + carriage position). This analog sensor measures the **tonearm pivot deflection
(tangency error)** — the controller moves the carriage to null it so the arm tracks straight across
the record. Different quantity, different sensor, hence a dedicated ADC.

### Single-ended vs differential
The **STM32F407 ADC is single-ended only** (no hardware differential mode — that's F3/G4/H7). So:
- single-ended sensor (pot / ratiometric Hall / AS5600 analog out) → one channel, ground-referenced;
- differential sensor → **pseudo-differential**: sample two channels, subtract in firmware (software
  common-mode/thermal-drift rejection).

To keep both paths open at zero pin cost (both free), provision an **adjacent pair**:
- **PC0 = ADC1_IN10** — primary arm-angle input.
- **PC1 = ADC1_IN11** — reserved second leg; ignore if the sensor is single-ended.

### Bit depth & speed
- **12-bit** native (4096 counts ≈ 0.007°/LSB over ±15°). The signal is mechanically slow
  (eccentricity ≈ 0.56 Hz at 33⅓ + slow drift), so **software-oversample/decimate** (e.g. average 64
  samples) for ~+3 effective bits and noise rejection. F407 has **no hardware oversampler** —
  accumulate over the DMA buffer in firmware.
- **Continuous + DMA (circular)**, **sample time 480 cycles** (tolerates a high-Z pot source), ADC
  clock **/4 = 21 MHz** → ~40 kSPS raw, decimated to a clean ~1 kHz. The carriage loop reads the
  latest DMA word **non-blocking** — never run a blocking conversion inside the 20 kHz gimbal ISR.

### CubeMX
> **Activate ADC1 in the GUI — do NOT hand-edit the `.ioc` for the ADC.** A hand-written `ADC1.*`
> block was silently dropped on the first regenerate (PC0 left as an orphan `ADCx_IN10` + a dangling
> `DMA2_Stream0`). Tick the channels in the tool so CubeMX instantiates the peripheral.

- `Analog → ADC1`: tick **IN10** (PC0), **Temperature Sensor Channel**, and **Vrefint Channel**
  (the temp sensor + Vrefint share the F407 `TSVREFE` enable). For pseudo-diff also tick **IN11** (PC1).
- *Parameter Settings*:
  - *Clock Prescaler*: **PCLK2 divided by 4**
  - *Resolution*: **12 bits**
  - *Scan Conversion Mode*: **Enabled** (multiple ranks)
  - *Continuous Conversion Mode*: **Enabled**
  - *DMA Continuous Requests*: **Enabled**
  - *Number Of Conversion*: **3** (arm angle, temp, Vrefint)
  - *Rank 1*: Channel **10** (PC0), *Sampling Time* **480 Cycles**
  - *Rank 2*: Channel **Temperature Sensor**, **480 Cycles** (temp sensor needs ≥10 µs settle)
  - *Rank 3*: Channel **Vrefint**, **480 Cycles**
- `DMA Settings`: add **ADC1** → **DMA2 Stream0**, *Mode* **Circular**, *Data Width* **Half Word**,
  Memory increment **on**. The DMA fills `uint16_t adc_dma[3] = { arm_angle, temp, vref }` each scan.
- `NVIC`: DMA2_Stream0 IRQ optional (circular buffer can be read without it).

### Internal core-temperature (status readout)
The F407 temp sensor is an **internal ADC1 channel — no pin**. It is slow (the 480-cycle sample time
above covers its ≥10 µs settling) and **has no factory calibration** on F407, so it uses the datasheet
typicals (rough, ±1–2 °C-ish — a "is the board hot" indicator, not metrology):

```
Vsense = adc_dma[1] * Vdda / 4095          // Vdda in volts
T_degC = (Vsense - 0.76) / 0.0025 + 25      // V25 = 0.76 V, slope = 2.5 mV/°C
```

Refine `Vdda` from the Vrefint rank (Vrefint *does* have a factory cal word at `0x1FFF7A2A`,
measured at Vdda = 3.3 V):

```
Vdda = 3.3 * VREFINT_CAL / adc_dma[2]       // VREFINT_CAL = *(uint16_t*)0x1FFF7A2A
```

Compute in **fixed-point** (e.g. centi-°C `int32_t`) — `TRACE`/the ScreenKey status path takes no
`%f`. Surface it on the ScreenKey **status** view next to the motor/diagnostic state.

---

## 6. Generated artifacts (after Generate Code)

- `tim.c/.h`: `htim2` + `MX_TIM2_Init()` (encoder mode, 32-bit) and `htim12` + `MX_TIM12_Init()`
  (PWM CH2, 50 Hz); MspPostInit puts PA0/PA1 on **AF1_TIM2** and PB15 on **AF9_TIM12**.
- `adc.c/.h`: `hadc1` + `MX_ADC1_Init()` (3 ranks: IN10, temp sensor, Vrefint); `dma.c` adds
  `DMA2_Stream0` for ADC1.
- `can.c/.h`: `hcan1` + `MX_CAN1_Init()`; PB8/PB9 on **AF9_CAN1**.
- `gpio.c`: `PLAT_INDEX` (PA2) and `ARM_HOME` (PE7) as EXTI inputs, pull-up.
- `main.h`: `PLAT_INDEX_Pin/_GPIO_Port`, `ARM_HOME_Pin/_GPIO_Port`.
- `stm32f4xx_it.c`: `EXTI2_IRQHandler`, `EXTI9_5_IRQHandler`, `CAN1_RX0_IRQHandler`.
- `main.c`: `MX_TIM2_Init()`, `MX_TIM12_Init()`, `MX_ADC1_Init()`, `MX_CAN1_Init()` calls.

## 7. Driver software (to add after generation)
- ABI capture adapter reading `TIM2->CNT` (+ index-latched zero) → platter angle/velocity.
- `ARM_HOME` consumed by the carriage homing routine (drive toward home until EXTI fires, zero there).
- CAN protocol layer over `hcan1`.
- Lift servo helper writing `TIM12->CCR2` (µs → counts; 1 µs/count).
- Arm-angle ADC: circular DMA buffer + decimating average → outer carriage-servo error term.
- Core-temperature readout: **DONE** — `App/platform/board_temp.{h,cpp}` starts ADC1 scan+DMA at
  boot and converts `dma_buf[1]` (temp) + `dma_buf[2]` (Vrefint, factory cal `0x1FFF7A2A`) to whole
  °C. Surfaced on the ScreenKey **System Status** screen's SYSTEM key (`"<n>C HOME OK"`); plumbed via
  `hmi::present(..., board_temp_c)` → `status_view`. Host-tested (`42C HOME OK`).
