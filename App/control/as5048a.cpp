/**
 * as5048a.cpp - AS5048A absolute encoder driver (SPI2, mode 1, 16-bit frames).
 *
 * 16-bit command: [PAR(even) | RWn(1=read) | addr<13:0>]. Read ANGLE (0x3FFF) command,
 * with parity and read bit set, is 0xFFFF. The response frame is [PAR | EF | data<13:0>],
 * and carries the data of the *previous* read - so we send the read-angle command twice
 * and take the second response. The even-parity check validates the framing on the bench.
 *
 * Raw bounded-spin SPI2 (no HAL): the tonearm FOC ISR (arm_foc) reads this device once per PWM
 * period (20 kHz), so we must NOT use HAL_SPI_TransmitReceive's HAL_MAX_DELAY (which would hang the
 * ISR forever if a status flag never sets). Mirrors the MT6826S hardening (see mt6826s.cpp). SPI2 is
 * configured by CubeMX (16-bit, mode 1, soft-NSS); we just drive the data register directly.
 */
#include "as5048a.h"
#include "gpio.hpp"

namespace as5048a {
namespace {

constexpr uint16_t kReadAngle = 0xFFFF; /* read reg 0x3FFF, RWn=1, even parity */

const gpio::OutputPin cs{AS_CS_GPIO_Port, AS_CS_Pin, /*active_high=*/false}; /* active-low */

constexpr uint32_t kSpiSpin = 100000; /* ~ms-scale guard; returns false instead of hanging */

/* Even parity of all set bits in v. */
inline bool parity(uint16_t v)
{
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return v & 1u;
}

/* One CS-framed 16-bit full-duplex frame over SPI2, raw and bounded. The AS5048A latches each
 * 16-bit frame on its own CS pulse, so each transfer toggles CS. Returns false on a stuck flag. */
bool transfer(uint16_t value, uint16_t& rx)
{
    SPI_TypeDef* const s = SPI2;
    s->CR1 |= SPI_CR1_SPE; /* ensure the peripheral is enabled */
    rx = 0;

    cs.on();
    uint32_t g = kSpiSpin;
    while (!(s->SR & SPI_SR_TXE)) if (--g == 0) { cs.off(); return false; }
    s->DR = value;                                 /* 16-bit data frame (DFF=16) */
    g = kSpiSpin;
    while (!(s->SR & SPI_SR_RXNE)) if (--g == 0) { cs.off(); return false; }
    rx = static_cast<uint16_t>(s->DR);
    g = kSpiSpin;
    while (s->SR & SPI_SR_BSY) if (--g == 0) { cs.off(); return false; }
    cs.off();
    return true;
}

}  // namespace

void read(Reading& out)
{
    uint16_t r0 = 0, r1 = 0;
    bool ok = transfer(kReadAngle, r0);       /* issue read-angle (response is stale, discard) */
    ok = transfer(kReadAngle, r1) && ok;      /* response = the angle requested above */

    out.raw        = r1;
    out.angle      = r1 & 0x3FFF;
    out.error_flag = (r1 >> 14) & 0x1u;
    /* parity_ok doubles as the transfer-valid flag: false on a bounded-spin timeout too, so the
     * FOC ISR can hold its last good angle on a bad read (mirrors MT6826S crc_ok). */
    out.parity_ok  = ok && (parity(r1 & 0x7FFFu) == ((r1 >> 15) & 0x1u));
}

}  // namespace as5048a
