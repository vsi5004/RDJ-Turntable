/**
 * as5048a.cpp - AS5048A absolute encoder driver (SPI2, mode 1).
 *
 * 16-bit command: [PAR(even) | RWn(1=read) | addr<13:0>]. Read ANGLE (0x3FFF) command,
 * with parity and read bit set, is 0xFFFF. The response frame is [PAR | EF | data<13:0>],
 * and carries the data of the *previous* read - so we send the read-angle command twice
 * and take the second response. The even-parity check validates the framing on the bench.
 */
#include "as5048a.h"
#include "spi.h" /* hspi2 */
#include "gpio.hpp"

namespace as5048a {
namespace {

constexpr uint16_t kReadAngle = 0xFFFF; /* read reg 0x3FFF, RWn=1, even parity */

const gpio::OutputPin cs{AS_CS_GPIO_Port, AS_CS_Pin, /*active_high=*/false}; /* active-low */

/* Even parity of all set bits in v. */
inline bool parity(uint16_t v)
{
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return v & 1u;
}

/* One 16-bit full-duplex frame; returns the slave's response. */
uint16_t transfer(uint16_t value)
{
    uint16_t rx = 0;
    cs.on();
    HAL_SPI_TransmitReceive(&hspi2, reinterpret_cast<uint8_t*>(&value),
                            reinterpret_cast<uint8_t*>(&rx), 1, HAL_MAX_DELAY);
    cs.off();
    return rx;
}

}  // namespace

void read(Reading& out)
{
    transfer(kReadAngle);                    /* issue read-angle (response is stale, discard) */
    uint16_t resp = transfer(kReadAngle);    /* response = the angle requested above */

    out.raw        = resp;
    out.angle      = resp & 0x3FFF;
    out.error_flag = (resp >> 14) & 0x1u;
    out.parity_ok  = parity(resp & 0x7FFFu) == ((resp >> 15) & 0x1u);
}

}  // namespace as5048a
