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

}  // namespace mt6826s
