# RDJ-Turntable

Firmware for a fully custom, motorized hi-fi turntable built around an **STM32F407VET6**
(board: FK407M1-V1.1). Belt-driven platter under field-oriented motor control, a
linear-tracking tonearm with servo-follow, CAN connectivity, and a screen-based HMI built
from Waveshare ScreenKey modules.

The full mechanical/electrical/control rationale and all settled design decisions live in
[record-player-design-log.md](record-player-design-log.md).

## Design goals

- **Native STM32 HAL**, not Arduino — clean, professional firmware. Drivers and the control
  loops are hand-rolled.
- **FOC in a high-priority timer ISR**; CAN / GUI / housekeeping in a non-blocking main loop.
- Deterministic real-time control protected from GUI activity.

## Hardware

| | |
|---|---|
| MCU | STM32F407VET6 (Cortex-M4F @ 168 MHz, board FK407M1-V1.1) |
| Clock | 8 MHz HSE crystal → 168 MHz |
| Debug | SWD via SEGGER J-Link (or an ST-Link); logging over SEGGER RTT |
| Displays | 3× Waveshare ScreenKey (128×128 ST7735) on SPI3 |

See the design log for the full subsystem list (motors, encoders, CAN, power).

## Toolchain

Native STM32 HAL project, configured with **STM32CubeMX**, built with **CMake + Ninja** and
**arm-none-eabi-gcc** — no Arduino.

**Prerequisites**
- [STM32CubeMX](https://www.st.com/en/development-tools/stm32cubemx.html) (peripheral/pin config; free myST account)
- Arm GNU Toolchain (`arm-none-eabi-gcc`), CMake ≥ 3.22, Ninja
- SEGGER J-Link software (GDB server + RTT)
- VS Code with the **cortex-debug** extension

## Build

```sh
cmake --preset Debug
cmake --build --preset Debug
```

Output: `build/Debug/RDJ-Turntable.elf`.

## Flash & debug

In VS Code, press **F5** (the `Debug (J-Link)` launch config): it builds, flashes over SWD,
and halts at `main`. Firmware logs stream over **SEGGER RTT** (channel 0) — viewable in the
cortex-debug RTT terminal or `JLinkRTTClient`.

Wire the J-Link to the FK407M1 SWD header: SWDIO=PA13, SWCLK=PA14, GND, and VTref→3V3.
Power the board from USB / regulated 5 V (**not** the 24 V system rail) during bring-up.

## Project layout

```
App/                 hand-written firmware
  platform/board.h   board pin map (LED, etc.)
  trace.h            SEGGER RTT logging shim
  app.c              top-level init + non-blocking main loop
  hmi/               ST7735 display driver + N-screen layer
Core/                CubeMX-generated HAL init (clocks, peripherals, main.c)
Drivers/             STM32F4 HAL + CMSIS (vendored)
third_party/SEGGER_RTT/   vendored RTT
cmake/, CMakeLists.txt    CMake/Ninja build (CubeMX-generated, customized)
RDJ-Turntable.ioc    CubeMX project (source of truth for pins/clocks/peripherals)
docs/                per-milestone CubeMX setup guides
```

The seam between generated and hand-written code: CubeMX's `main.c` calls `app_init()` and
`app_run()` (in its USER CODE blocks); everything under `App/` is hand-written.

## Status

- **M0** — toolchain, LED blink, SWD debug, RTT, 168 MHz clock ✅
- **M1** — ScreenKey displays: SPI3 + DMA driver, N-screen array, keys ✅
- **M2** — single-motor FOC (next)
- M3 — platter speed loop · M4 — tonearm + cueing · M5 — CAN + HMI integration · M6 — tonearm servo-follow

See [docs/](docs/) for the CubeMX configuration applied at each milestone.
