/**
 * mt6826s.cpp - MT6826S absolute encoder driver (SPI1, mode 3).
 *
 * Burst-read angle: pull CS low, send {0xA0, 0x03} (cmd 0xA = burst read, start at reg 0x003),
 * then clock out 4 bytes = regs 0x003..0x006:
 *   0x003 = ANGLE[14:7]
 *   0x004 = ANGLE[6:0] << 1
 *   0x005 = STATUS[2:0]
 *   0x006 = CRC-8 (poly 0x07) over regs 0x003..0x005
 * angle15 = (0x003 << 7) | (0x004 >> 1). The CRC check validates the framing on the bench.
 */
#include "mt6826s.h"
#include "spi.h" /* hspi1 */
#include "gpio.hpp"

namespace mt6826s {
namespace {

constexpr uint8_t kCmdBurstRead = 0xA0; /* cmd 0b1010, address high nibble 0 */
constexpr uint8_t kAngleAddr    = 0x03; /* start register 0x003 */

const gpio::OutputPin cs{MT_CS_GPIO_Port, MT_CS_Pin, /*active_high=*/false}; /* active-low */

/* CRC-8, polynomial x^8+x^2+x+1 (0x07), init 0x00, MSB-first. */
uint8_t crc8(const uint8_t* data, int n)
{
    uint8_t crc = 0;
    for (int i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07)
                               : static_cast<uint8_t>(crc << 1);
    }
    return crc;
}

}  // namespace

void read(Reading& out)
{
    uint8_t tx[6] = { kCmdBurstRead, kAngleAddr, 0, 0, 0, 0 };
    uint8_t rx[6] = { 0 };

    cs.on();
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, sizeof(tx), HAL_MAX_DELAY);
    cs.off();

    /* rx[0..1] clocked out during the command bytes; rx[2..5] = regs 0x003..0x006 */
    out.raw[0] = rx[2];
    out.raw[1] = rx[3];
    out.raw[2] = rx[4];
    out.raw[3] = rx[5];
    out.angle  = static_cast<uint16_t>((rx[2] << 7) | (rx[3] >> 1));
    out.status = rx[4] & 0x07;                 /* STATUS[2:0] */
    out.crc_ok = (crc8(&rx[2], 3) == rx[5]);   /* CRC covers regs 0x003..0x005 */
}

namespace {

constexpr uint8_t kCmdReadReg  = 0x30; /* cmd 0b0011 in the high nibble */
constexpr uint8_t kCmdWriteReg = 0x60; /* cmd 0b0110 */
constexpr uint8_t kCmdProgEE   = 0xC0; /* cmd 0b1100 */

constexpr uint16_t kRegAutoCalFreq = 0x00E; /* [6:4] = AUTOCAL_FREQ */
constexpr uint16_t kRegEeDone      = 0x112; /* [5] = EE_DONE */
constexpr uint16_t kRegCalStatus   = 0x113; /* [7:6] = auto-cal status */
constexpr uint16_t kRegCalTrigger  = 0x155; /* 0x5E = start, 0x00 = stop */

/* Run a 3-byte SPI1 transaction with interrupts masked, so the closed-loop FOC ISR (which also
 * burst-reads this device) can't preempt and interleave on the bus. ~3 us masked = harmless. */
void xfer3(const uint8_t tx[3], uint8_t rx[3])
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    cs.on();
    HAL_SPI_TransmitReceive(&hspi1, const_cast<uint8_t*>(tx), rx, 3, HAL_MAX_DELAY);
    cs.off();
    if (!primask) __enable_irq();
}

}  // namespace

uint8_t read_reg(uint16_t addr)
{
    uint8_t tx[3] = { static_cast<uint8_t>(kCmdReadReg | ((addr >> 8) & 0x0F)),
                      static_cast<uint8_t>(addr & 0xFF), 0x00 };
    uint8_t rx[3] = { 0 };
    xfer3(tx, rx);
    return rx[2]; /* data clocked out on the 3rd byte */
}

void write_reg(uint16_t addr, uint8_t data)
{
    uint8_t tx[3] = { static_cast<uint8_t>(kCmdWriteReg | ((addr >> 8) & 0x0F)),
                      static_cast<uint8_t>(addr & 0xFF), data };
    uint8_t rx[3] = { 0 };
    xfer3(tx, rx);
}

void set_autocal_freq(uint8_t freq3)
{
    uint8_t v = read_reg(kRegAutoCalFreq);
    v = static_cast<uint8_t>((v & ~0x70) | ((freq3 & 0x07) << 4)); /* preserve other bits */
    write_reg(kRegAutoCalFreq, v);
}

void start_autocal() { write_reg(kRegCalTrigger, 0x5E); }
void stop_autocal()  { write_reg(kRegCalTrigger, 0x00); }

CalStatus autocal_status()
{
    return static_cast<CalStatus>((read_reg(kRegCalStatus) >> 6) & 0x03);
}

bool program_eeprom()
{
    /* cmd 1100, address 0; device acks 0x55, then EE_DONE (reg 0x112[5]) rises when written. */
    uint8_t tx[3] = { kCmdProgEE, 0x00, 0x00 };
    uint8_t rx[3] = { 0 };
    xfer3(tx, rx);
    if (rx[2] != 0x55) return false; /* no ack */

    for (int i = 0; i < 100; ++i) {  /* EEPROM write is a few ms; poll up to ~100 ms */
        if (read_reg(kRegEeDone) & (1u << 5)) return true;
        HAL_Delay(1);
    }
    return false;
}

}  // namespace mt6826s
