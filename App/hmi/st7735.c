/**
 * st7735.c — ST7735 driver for the Waveshare ScreenKey panels on shared SPI3.
 *
 * SPI3 is 8-bit, MSB-first, mode 0, TX-only with DMA on the pixel push.
 * Init sequence mirrors Adafruit's INITR_144GREENTAB + invertDisplay(true) (the
 * combination proven on the ESP32 reference), expressed as a command table.
 */
#include "st7735.h"
#include "display_config.h"
#include "spi.h" /* CubeMX: extern SPI_HandleTypeDef hspi3 */

/* ST7735 commands */
#define ST_SWRESET 0x01
#define ST_SLPOUT  0x11
#define ST_INVON   0x21
#define ST_DISPON  0x29
#define ST_CASET   0x2A
#define ST_RASET   0x2B
#define ST_RAMWR   0x2C
#define ST_MADCTL  0x36
#define ST_COLMOD  0x3A
#define ST_NORON   0x13
#define ST_FRMCTR1 0xB1
#define ST_FRMCTR2 0xB2
#define ST_FRMCTR3 0xB3
#define ST_INVCTR  0xB4
#define ST_PWCTR1  0xC0
#define ST_PWCTR2  0xC1
#define ST_PWCTR3  0xC2
#define ST_PWCTR4  0xC3
#define ST_PWCTR5  0xC4
#define ST_VMCTR1  0xC5
#define ST_GMCTRP1 0xE0
#define ST_GMCTRN1 0xE1

#define ST_DELAY 0x80 /* flag in the arg-count byte: a delay (ms) follows the args */

/* Table format (Adafruit-style): N commands, then per command:
 *   cmd, (ST_DELAY | numArgs), args..., [delayMs if ST_DELAY set]  (255 => 500 ms) */
static const uint8_t init_cmds[] = {
    19,                                                             /* command count */
    ST_SWRESET, ST_DELAY, 150,
    ST_SLPOUT,  ST_DELAY, 255,
    ST_FRMCTR1, 3, 0x01, 0x2C, 0x2D,
    ST_FRMCTR2, 3, 0x01, 0x2C, 0x2D,
    ST_FRMCTR3, 6, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D,
    ST_INVCTR,  1, 0x07,
    ST_PWCTR1,  3, 0xA2, 0x02, 0x84,
    ST_PWCTR2,  1, 0xC5,
    ST_PWCTR3,  2, 0x0A, 0x00,
    ST_PWCTR4,  2, 0x8A, 0x2A,
    ST_PWCTR5,  2, 0x8A, 0xEE,
    ST_VMCTR1,  1, 0x0E,
    ST_INVON,   0,                                                  /* invertDisplay(true) */
    ST_MADCTL,  1, 0xC8,                                            /* MX|MY|BGR, rotation 0 */
    ST_COLMOD,  1, 0x05,                                            /* 16 bit/pixel */
    ST_GMCTRP1, 16, 0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                    0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10,
    ST_GMCTRN1, 16, 0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                    0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10,
    ST_NORON,  ST_DELAY, 10,
    ST_DISPON, ST_DELAY, 100,
};

static inline void dc_command(void) { HAL_GPIO_WritePin(DISP_DC_GPIO_Port, DISP_DC_Pin, GPIO_PIN_RESET); }
static inline void dc_data(void)    { HAL_GPIO_WritePin(DISP_DC_GPIO_Port, DISP_DC_Pin, GPIO_PIN_SET); }
static inline void cs_select(const st7735_t *d)   { HAL_GPIO_WritePin(d->cs_port, d->cs_pin, GPIO_PIN_RESET); }
static inline void cs_deselect(const st7735_t *d) { HAL_GPIO_WritePin(d->cs_port, d->cs_pin, GPIO_PIN_SET); }

static void tx(const uint8_t *data, uint16_t n)
{
    HAL_SPI_Transmit(&hspi3, (uint8_t *)data, n, HAL_MAX_DELAY);
}

static void write_cmd(uint8_t cmd)
{
    dc_command();
    tx(&cmd, 1);
}

static void write_data(const uint8_t *data, uint16_t n)
{
    dc_data();
    tx(data, n);
}

static void run_init_list(const uint8_t *addr)
{
    uint8_t numCmds = *addr++;
    while (numCmds--) {
        uint8_t cmd = *addr++;
        uint8_t x   = *addr++;
        uint8_t numArgs = x & 0x7F;
        write_cmd(cmd);
        if (numArgs) {
            write_data(addr, numArgs);
            addr += numArgs;
        }
        if (x & ST_DELAY) {
            uint16_t ms = *addr++;
            HAL_Delay(ms == 255 ? 500 : ms);
        }
    }
}

/* Set the RAM write window (applies the panel's 2,3 offset) and leave it in RAMWR. */
static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    x0 += ST7735_COL_OFFSET; x1 += ST7735_COL_OFFSET;
    y0 += ST7735_ROW_OFFSET; y1 += ST7735_ROW_OFFSET;
    uint8_t caset[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    uint8_t raset[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    write_cmd(ST_CASET); write_data(caset, 4);
    write_cmd(ST_RASET); write_data(raset, 4);
    write_cmd(ST_RAMWR);
}

void st7735_hw_reset(void)
{
    HAL_GPIO_WritePin(DISP_RST_GPIO_Port, DISP_RST_Pin, GPIO_PIN_SET);   HAL_Delay(5);
    HAL_GPIO_WritePin(DISP_RST_GPIO_Port, DISP_RST_Pin, GPIO_PIN_RESET); HAL_Delay(20);
    HAL_GPIO_WritePin(DISP_RST_GPIO_Port, DISP_RST_Pin, GPIO_PIN_SET);   HAL_Delay(150);
}

void st7735_backlight(uint8_t on)
{
    HAL_GPIO_WritePin(DISP_BL_GPIO_Port, DISP_BL_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void st7735_init(const st7735_t *d)
{
    cs_select(d);
    run_init_list(init_cmds);
    cs_deselect(d);
}

void st7735_fill(const st7735_t *d, uint16_t color)
{
    uint16_t line[ST7735_W];
    for (int i = 0; i < ST7735_W; i++) line[i] = color;

    cs_select(d);
    set_window(0, 0, ST7735_W - 1, ST7735_H - 1);
    dc_data();
    for (int y = 0; y < ST7735_H; y++)
        tx((const uint8_t *)line, sizeof(line));
    cs_deselect(d);
}

void st7735_draw(const st7735_t *d, const uint16_t *fb)
{
    cs_select(d);
    set_window(0, 0, ST7735_W - 1, ST7735_H - 1);
    dc_data();
    HAL_SPI_Transmit_DMA(&hspi3, (uint8_t *)fb, ST7735_PIXELS * 2);
    while (HAL_SPI_GetState(&hspi3) != HAL_SPI_STATE_READY) { /* wait DMA complete */ }
    cs_deselect(d);
}
