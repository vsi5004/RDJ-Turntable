/**
 * as5048a.h - AMS AS5048A 14-bit absolute magnetic encoder (tonearm motor) on SPI2.
 *
 * SPI mode 1, MSB-first, 16-bit frames, <=10 MHz. Reads are pipelined: the response to a
 * command arrives on the *next* transfer, so reading the angle takes two transfers.
 */
#pragma once
#include "main.h" /* HAL + AS_CS pin label */
#include <cstdint>

namespace as5048a {

struct Reading {
    uint16_t raw;        /* full 16-bit response frame */
    uint16_t angle;      /* 14-bit, 0..16383 (full turn) */
    bool     error_flag; /* EF bit: a prior transmission error (clear via reg 0x0001) */
    bool     parity_ok;  /* response even-parity matched */
};

/* Read the absolute angle over SPI2 (issues two read-angle frames). */
void read(Reading& out);

/* 14-bit count -> degrees (for the FOC layer; not for RTT printf, which has no %f). */
constexpr float degrees(uint16_t count) { return count * (360.0f / 16384.0f); }

}  // namespace as5048a
