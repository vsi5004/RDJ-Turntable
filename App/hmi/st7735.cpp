/**
 * st7735.cpp - ST7735 driver for the Waveshare ScreenKey panels on shared SPI3.
 *
 * SPI3 is 8-bit, MSB-first, mode 0, TX-only with DMA on the pixel push.
 * Init sequence mirrors Adafruit's INITR_144GREENTAB + invertDisplay(true) (the
 * combination proven on the ESP32 reference), expressed as a command table.
 */
#include "st7735.h"
#include "display_config.h"
#include "spi.h" /* hspi3 */

namespace st7735 {
namespace {

/* ST7735 commands */
constexpr uint8_t kSwReset = 0x01;
constexpr uint8_t kSlpOut  = 0x11;
constexpr uint8_t kInvOn   = 0x21;
constexpr uint8_t kDispOn  = 0x29;
constexpr uint8_t kCaset   = 0x2A;
constexpr uint8_t kRaset   = 0x2B;
constexpr uint8_t kRamWr   = 0x2C;
constexpr uint8_t kMadCtl  = 0x36;
constexpr uint8_t kColMod  = 0x3A;
constexpr uint8_t kNorOn   = 0x13;
constexpr uint8_t kFrmCtr1 = 0xB1;
constexpr uint8_t kFrmCtr2 = 0xB2;
constexpr uint8_t kFrmCtr3 = 0xB3;
constexpr uint8_t kInvCtr  = 0xB4;
constexpr uint8_t kPwCtr1  = 0xC0;
constexpr uint8_t kPwCtr2  = 0xC1;
constexpr uint8_t kPwCtr3  = 0xC2;
constexpr uint8_t kPwCtr4  = 0xC3;
constexpr uint8_t kPwCtr5  = 0xC4;
constexpr uint8_t kVmCtr1  = 0xC5;
constexpr uint8_t kGmCtrP1 = 0xE0;
constexpr uint8_t kGmCtrN1 = 0xE1;

constexpr uint8_t kDelay = 0x80; /* flag in the arg-count byte: a delay (ms) follows the args */

/* Table (Adafruit-style): N commands, then per command:
 *   cmd, (kDelay | numArgs), args..., [delayMs if kDelay set]  (255 => 500 ms) */
const uint8_t kInitCmds[] = {
    18,                                                             /* command count */
    /* hw_reset() already did a hardware reset, so software SWRESET is redundant - dropped. */
    kSlpOut,  kDelay, 120,   /* datasheet minimum wait after sleep-out (was 500) */
    kFrmCtr1, 3, 0x01, 0x2C, 0x2D,
    kFrmCtr2, 3, 0x01, 0x2C, 0x2D,
    kFrmCtr3, 6, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D,
    kInvCtr,  1, 0x07,
    kPwCtr1,  3, 0xA2, 0x02, 0x84,
    kPwCtr2,  1, 0xC5,
    kPwCtr3,  2, 0x0A, 0x00,
    kPwCtr4,  2, 0x8A, 0x2A,
    kPwCtr5,  2, 0x8A, 0xEE,
    kVmCtr1,  1, 0x0E,
    kInvOn,   0,                                                    /* invertDisplay(true) */
    kMadCtl,  1, 0xC8,                                             /* MX|MY|BGR, rotation 0 */
    kColMod,  1, 0x05,                                             /* 16 bit/pixel */
    kGmCtrP1, 16, 0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                  0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10,
    kGmCtrN1, 16, 0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                  0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10,
    kNorOn,  kDelay, 10,
    kDispOn, kDelay, 20,     /* backlight is off during init, so no settle needed (was 100) */
};

/* Shared control pins. */
const gpio::OutputPin dc {DISP_DC_GPIO_Port,  DISP_DC_Pin};         /* on()=data, off()=command */
const gpio::OutputPin rst{DISP_RST_GPIO_Port, DISP_RST_Pin, false}; /* reset is active-low */
const gpio::OutputPin bl {DISP_BL_GPIO_Port,  DISP_BL_Pin};         /* assume active-high (verify) */

inline void dc_command() { dc.off(); }
inline void dc_data()    { dc.on(); }
inline void cs_select(const Panel& p)   { p.cs.on(); }
inline void cs_deselect(const Panel& p) { p.cs.off(); }

void tx(const uint8_t* data, uint16_t n)
{
    HAL_SPI_Transmit(&hspi3, const_cast<uint8_t*>(data), n, HAL_MAX_DELAY);
}

void write_cmd(uint8_t cmd)
{
    dc_command();
    tx(&cmd, 1);
}

void write_data(const uint8_t* data, uint16_t n)
{
    dc_data();
    tx(data, n);
}

/* Set the RAM write window (applies the panel's 2,3 offset) and leave it in RAMWR. */
void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    x0 += ST7735_COL_OFFSET; x1 += ST7735_COL_OFFSET;
    y0 += ST7735_ROW_OFFSET; y1 += ST7735_ROW_OFFSET;
    uint8_t caset[4] = { static_cast<uint8_t>(x0 >> 8), static_cast<uint8_t>(x0 & 0xFF),
                         static_cast<uint8_t>(x1 >> 8), static_cast<uint8_t>(x1 & 0xFF) };
    uint8_t raset[4] = { static_cast<uint8_t>(y0 >> 8), static_cast<uint8_t>(y0 & 0xFF),
                         static_cast<uint8_t>(y1 >> 8), static_cast<uint8_t>(y1 & 0xFF) };
    write_cmd(kCaset); write_data(caset, 4);
    write_cmd(kRaset); write_data(raset, 4);
    write_cmd(kRamWr);
}

}  // namespace

void hw_reset()
{
    rst.off(); HAL_Delay(5);   /* released */
    rst.on();  HAL_Delay(20);  /* asserted (low) */
    rst.off(); HAL_Delay(150); /* released */
}

void backlight(bool on) { bl.write(on); }

void init_all(const Panel** panels, int count)
{
    /* Interleaved: send each command to every panel, then pay each stabilisation delay
     * ONCE - so the SLPOUT/etc. waits overlap and boot time is independent of panel count. */
    const uint8_t* addr = kInitCmds;
    uint8_t numCmds = *addr++;
    while (numCmds--) {
        uint8_t cmd     = *addr++;
        uint8_t x       = *addr++;
        uint8_t numArgs = x & 0x7F;
        const uint8_t* args = addr;
        for (int i = 0; i < count; ++i) {
            cs_select(*panels[i]);
            write_cmd(cmd);
            if (numArgs) write_data(args, numArgs);
            cs_deselect(*panels[i]);
        }
        addr += numArgs;
        if (x & kDelay) {
            uint16_t ms = *addr++;
            HAL_Delay(ms == 255 ? 500 : ms);
        }
    }
}

void fill(const Panel& p, uint16_t color)
{
    uint16_t line[ST7735_W];
    for (int i = 0; i < ST7735_W; ++i) line[i] = color;

    cs_select(p);
    set_window(0, 0, ST7735_W - 1, ST7735_H - 1);
    dc_data();
    for (int y = 0; y < ST7735_H; ++y)
        tx(reinterpret_cast<const uint8_t*>(line), sizeof(line));
    cs_deselect(p);
}

void draw(const Panel& p, const uint16_t* fb)
{
    cs_select(p);
    set_window(0, 0, ST7735_W - 1, ST7735_H - 1);
    dc_data();
    HAL_SPI_Transmit_DMA(&hspi3, reinterpret_cast<uint8_t*>(const_cast<uint16_t*>(fb)),
                         ST7735_PIXELS * 2);
    while (HAL_SPI_GetState(&hspi3) != HAL_SPI_STATE_READY) { /* wait DMA complete */ }
    cs_deselect(p);
}

}  // namespace st7735
