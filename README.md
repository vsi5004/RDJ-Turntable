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

## Host tests

The product state machine, diagnostic authority, event handling, and platter-feedback algorithms are
HAL-free and have a native C++ test target. Docker provides the pinned GCC 14 environment:

```powershell
docker build -t rdj-turntable-tests -f tests/Dockerfile .
docker run --rm --mount type=bind,source="${PWD}",target=/src rdj-turntable-tests `
  bash /src/tests/run-host-tests.sh
```

Without Docker, configure `tests/` as a standalone CMake project using any C++17 desktop compiler.

## Host simulator

The same host build also produces `turntable_sim`, a deterministic full-system simulator backed by
the production state-machine interfaces. It models platter acceleration and lock, carriage homing
and travel, the timed tonearm lift, injected faults, and device stalls without requiring hardware.

Run one of the checked-in scenarios through Docker:

```powershell
docker run --rm --mount type=bind,source="${PWD}",target=/src rdj-turntable-tests `
  bash /src/tests/run-simulator.sh nominal
```

Scenario commands include `initialize`, `play`, `pause`, `resume`, `stop`, `speed 33|45`,
`end-side`, `wait`, `await`, `expect`, `stall`, and recoverable or home-invalidating `fault`
injection. The scenarios under `simulator/scenarios/` are also executed by the host test suite.
See [simulator/README.md](simulator/README.md) for the scenario language and fault options.

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

The approved full-system operational model and three-ScreenKey behavior are specified in
[docs/turntable-state-machine.md](docs/turntable-state-machine.md).

During mechanical bring-up, development firmware boots into the diagnostic authority. The existing
M2c platter spin, alignment, encoder calibration, and velocity-loop exercises remain available through
the three ScreenKeys without bypassing the new single-owner control architecture.
