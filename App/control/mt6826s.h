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

/* --- Single-register access (cmd 0011 read / 0110 write). Each transfer brackets itself with
 *     a global IRQ mask so it can't collide with the FOC ISR's burst read on the same SPI1. --- */
uint8_t read_reg(uint16_t addr);
void    write_reg(uint16_t addr, uint8_t data);

/* --- M2c-4 user auto-calibration (datasheet Rev 1.1 §10.2). Requires a CONSTANT speed in the
 *     band selected by AUTOCAL_FREQ, for >18 revolutions. EEPROM endures only 1000 programs. --- */
enum class CalStatus : uint8_t { None = 0, Running = 1, Failed = 2, Success = 3 };

/* AUTOCAL_FREQ[2:0] = reg 0x00E[6:4]. 0x4 = 200-400 RPM band. Read-modify-write (volatile RAM). */
void set_autocal_freq(uint8_t freq3);

void      start_autocal();   /* write 0x5E -> 0x155 (begins the run; spin steadily) */
void      stop_autocal();    /* write 0x00 -> 0x155 (exit on success) */
CalStatus autocal_status();  /* reg 0x113[7:6] */

/* Persist calibration to EEPROM (cmd 1100). DESTRUCTIVE: consumes one of ~1000 cycles.
 * Returns true if the ack + EE_DONE handshake completed. Framing per the distilled doc;
 * verify against the datasheet before relying on it. */
bool program_eeprom();

}  // namespace mt6826s
