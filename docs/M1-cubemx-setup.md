# Milestone 1 — CubeMX additions: SPI3 + DMA + ScreenKey GPIOs

Adds the display bus and per-screen control pins for the Waveshare ScreenKey modules
(128×128 ST7735, write-only SPI). Open `RDJ-Turntable.ioc` in CubeMX and apply the
following, then **Generate Code**. My `App/hmi/` driver references the labels below, so
spelling them exactly matters.

## 1. SPI3 peripheral

**Connectivity → SPI3**:
- *Mode*: **Transmit Only Master**  (uses SCK + MOSI only; no MISO needed)
- *Hardware NSS Signal*: **Disable**  (we drive CS by GPIO)
- **Parameter Settings**:
  - *Frame Format*: Motorola
  - *Data Size*: **8 Bits**  (commands and pixel DMA share one config; the framebuffer stores RGB565 byte-swapped so bytes clock out high-byte-first for ST7735)
  - *First Bit*: **MSB First**
  - *Prescaler*: **2**  → 42 MHz PCLK1 / 2 = **21 MHz** (the max SPI3 can do — it's on APB1; 40 MHz would need SPI1, which we reserve for the encoders)
  - *Clock Polarity (CPOL)*: **Low**
  - *Clock Phase (CPHA)*: **1 Edge**   (CPOL=0/CPHA=0 = SPI mode 0)

After enabling SPI3, the SCK/MOSI may auto-place on PC10/PC12. **Reassign them:**
- Click **PB3** → **SPI3_SCK**
- Click **PB5** → **SPI3_MOSI**
(Leave PC10/PC11/PC12 free — they belong to the microSD slot.)

## 2. SPI3 TX DMA

**SPI3 → DMA Settings → Add**:
- *DMA Request*: **SPI3_TX**
- Stream: leave CubeMX default (DMA1 Stream 5 or 7)
- *Direction*: Memory To Peripheral
- *Priority*: **High**
- *Mode*: Normal
- *Increment Address*: **Memory** checked, Peripheral unchecked
- *Data Width*: Peripheral **Byte**, Memory **Byte**

**SPI3 → NVIC Settings**: ensure **SPI3 global interrupt** is enabled (the DMA stream IRQ
is enabled automatically). This gives us the transfer-complete callback.

## 3. GPIO pins + labels

For each, click the pin, choose the mode, then right-click → **Enter User Label**:

| Pin | Mode | User Label | GPIO settings |
|-----|------|-----------|---------------|
| PE2 | GPIO_Output | `DISP_DC`  | Output level High |
| PE3 | GPIO_Output | `DISP_RST` | Output level High (not-in-reset) |
| PD12 | GPIO_Output | `DISP_BL` | Output level Low (start off; PWM later) |
| PE4 | GPIO_Output | `DISP_CS0` | Output level High (deselected) |
| PE5 | GPIO_Output | `DISP_CS1` | Output level High |
| PE6 | GPIO_Output | `DISP_CS2` | Output level High |
| PE8 | GPIO_Input  | `KEY0` | Pull-up |
| PE9 | GPIO_Input  | `KEY1` | Pull-up |
| PE10 | GPIO_Input | `KEY2` | Pull-up |

For the two SPI3 AF pins (PB3/PB5), set **GPIO Output Speed = Very High** in their GPIO
settings for clean edges (CubeMX usually defaults this for SPI).

## 4. Generate

**GENERATE CODE**. My `app_init()`/`app_run()` hooks and the `App/`+`third_party/` CMake
entries survive (they're in USER CODE / the once-generated CMakeLists). Then reply
**"generated"** and I'll wire `App/hmi/` into `main.c`, build, and we light up the screens.

> Sanity: after generating, `Core/Inc/main.h` should contain `DISP_DC_Pin`, `DISP_CS0_Pin`,
> `KEY0_Pin`, etc., and a new `Core/Src/spi.c` + `dma.c`. If a label is missing/misspelled
> the build will tell us exactly which.
