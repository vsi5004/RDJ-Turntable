# Milestone 0 — STM32CubeMX Setup Guide

Goal: generate a CMake-based HAL project for the **STM32F407VET6** (board FK407M1-V1.1)
configured for a 168 MHz clock, an LED GPIO, and SWD debug — then we blink it and verify
the J-Link debug flow.

You only need to do the steps marked **[YOU]**. Everything else I handle once the project
is generated.

---

## 1. Install STM32CubeMX  **[YOU]**

1. Go to <https://www.st.com/en/development-tools/stm32cubemx.html> → **Get Software**.
2. Create / log in to a free **myST account** (required for the download).
3. Install CubeMX (default options). On first launch, let it install the **STM32CubeF4 MCU
   package** when prompted (Help → Manage embedded software packages → STM32F4 → install
   latest), or it will fetch it when you select the part.

> We do **not** need STM32CubeCLT or STM32CubeIDE — your J-Link covers flashing + debugging,
> and the Arm GCC / CMake / Ninja are already installed.

## 2. Create the project  **[YOU]**

1. **File → New Project** → in *Part Number Search* type `STM32F407VET6` → select it
   (package **LQFP100**) → **Start Project**.

## 3. Pinout & Configuration  **[YOU]**

In the **Pinout & Configuration** tab:

- **System Core → SYS**
  - *Debug*: **Serial Wire**  (enables SWD on PA13/PA14 — required for J-Link)
  - *Timebase Source*: **SysTick** (default)
- **System Core → RCC**
  - *High Speed Clock (HSE)*: **Crystal/Ceramic Resonator**
  - *Low Speed Clock (LSE)*: Disable (not needed for M0)
- **User LED GPIO** — **PC13** (confirmed from the FK407M1 hardware reference; **active-low**,
  i.e. the LED turns ON when PC13 is driven LOW)
  - In the chip view, **left-click pin `PC13` → select `GPIO_Output`**.
  - Right-click `PC13` → **Enter User Label** → type `LED`.
  - (Optional) In **GPIO settings**, set PC13 *Output level* = **High** so the LED starts OFF.

## 4. Clock Configuration  **[YOU]**

Switch to the **Clock Configuration** tab:

1. Set **Input frequency (HSE)** to **8** (MHz) — confirmed for the FK407M1.
2. In the **HCLK (MHz)** box, type **168** and press Enter → let CubeMX auto-solve. Then
   confirm the resulting tree (for the **8 MHz** HSE):
   - PLL Source Mux = **HSE**
   - *M* = **/8**, *N* = **×336**, *P* = **/2** → **SYSCLK = 168 MHz**
   - *Q* = **/7** → **48 MHz** (for USB/RNG/SDIO later)
   - System Clock Mux = **PLLCLK**
   - APB1 Prescaler = **/4** (PCLK1 = 42 MHz), APB2 Prescaler = **/2** (PCLK2 = 84 MHz)
   - Flash latency shows **5 WS** automatically
   - (If the crystal is 25 MHz, just keep HCLK = 168 and let CubeMX recompute M/N/P; the
     numbers differ but the result is the same 168 MHz.)

## 5. Project Manager  **[YOU]**

In the **Project Manager** tab:

- **Project** sub-tab:
  - *Project Name*: **`RDJ-Turntable`**
  - *Project Location*: **`C:\dev`**  (so it generates into the existing `C:\dev\RDJ-Turntable`)
  - *Toolchain / IDE*: **CMake**   ← important; this emits a CMake/Ninja project
- **Code Generator** sub-tab:
  - *STM32Cube MCU packages*: **Copy only the necessary library files**
  - Check **Generate peripheral initialization as a pair of `.c`/`.h` files per peripheral**
  - Check **Keep User Code when re-generating** (default)

## 6. Generate  **[YOU]**

Click **GENERATE CODE** (top-right). If it warns the directory isn't empty, that's fine —
proceed (it won't touch `LICENSE`, `docs/`, `.gitignore`, or the design log).

After it generates, the repo will contain: `CMakeLists.txt`, `CMakePresets.json`, `cmake/`,
`Core/`, `Drivers/`, `startup_stm32f407xx.s`, `STM32F407VETX_FLASH.ld`, and `RDJ-Turntable.ioc`.

---

## 7. Hand back to me

Reply with just ✅ **"generated"** once step 6 completes. (LED = PC13 active-low and
HSE = 8 MHz are already confirmed from the board reference, so nothing else to report —
though if your **V1.1** board's silkscreen disagrees with the V1.0 reference, flag it.)

Then I will: integrate the `App/` layer, wire the blink + SEGGER RTT trace, add the
`.vscode` J-Link build/debug configs, build it, and we verify the M0 exit criteria together.

---

### Board reference (FK407M1, from stm32-base.org V1.0 page)

| Item | Value |
|------|-------|
| HSE crystal | **8 MHz** |
| LSE crystal | 32.768 kHz |
| User LED | **PC13**, active-low (on = LOW) |
| User button (KEY) | PA15, active-low |
| SWD header | DIO=PA13, CLK=PA14 (also breaks out PA9/PA10 = USART1) |
| microSD | PD8/PD3/PC7/PC8/PC10/PC11 |
| USB | PA11 (D−) / PA12 (D+) |

