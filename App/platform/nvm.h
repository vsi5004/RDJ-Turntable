/**
 * nvm.h - tiny non-volatile store for commutation-critical calibration, in MCU flash.
 *
 * Holds the FOC alignment (zero electrical offset + direction sign) so the platter motor
 * commutates correctly from boot without re-aligning each power-on. Lives in internal flash
 * (not the removable SD card) precisely because the motor depends on it. One dedicated flash
 * sector; writes are rare (only on re-align), well within endurance.
 */
#pragma once
#include <cstdint>

namespace nvm {

struct Cal {
    float   zero_offset; /* mechanical angle (rad) where theta_elec == 0 */
    int32_t direction;   /* +1 / -1 */
};

/* Load the stored calibration. Returns false if none is present / fails validation. */
bool load(Cal& out);

/* Erase + program the calibration sector. Returns true on success. Stalls the CPU for the
 * duration of the sector erase (~1 s) — call only with the motor stopped. */
bool save(const Cal& in);

}  // namespace nvm
