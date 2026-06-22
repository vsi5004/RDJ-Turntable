/**
 * mt6826s.h - MagnTek MT6826S 15-bit absolute magnetic encoder (platter motor) on SPI1.
 *
 * SPI mode 3, MSB-first, <=16 MHz. Burst-read returns the angle + status + CRC.
 */
#pragma once
#include "main.h" /* HAL + MT_CS pin label */
#include <cstdint>

namespace mt6826s {

/* STATUS[2:0] warning bits from angle register 0x005. */
enum class Status : uint8_t {
    OverSpeed    = 1u << 0,
    WeakField    = 1u << 1,
    UnderVoltage = 1u << 2,
};

struct Reading {
    uint8_t  raw[4];  /* registers 0x003..0x006 as read */
    uint16_t angle;   /* 15-bit, 0..32767 (full turn) */
    uint8_t  status;  /* raw STATUS[2:0] bits */
    bool     crc_ok;  /* device CRC matched our computed CRC */

    bool has(Status f) const { return (status & static_cast<uint8_t>(f)) != 0; }
};

/* Burst-read the absolute angle (+ status + CRC) over SPI1. */
void read(Reading& out);

/* 15-bit count -> degrees (for the FOC layer; not for RTT printf, which has no %f). */
constexpr float degrees(uint16_t count) { return count * (360.0f / 32768.0f); }

}  // namespace mt6826s
