# MT6826S — User Auto-Calibration (platter motor encoder)

From the MT6826S datasheet Rev 1.1 (local copy: `docs/datasheets/MT6826S_Rev1.1.pdf`):
§8.6 SPI, §10.2 User Auto-Calibration, §11 Register Map. Captured/verified at **M2c** (needs the
motor spinning). Not needed for M2a (raw angle read).

## Why

Two calibration levels exist. **Factory calibration** (done at the factory) gives INL ≈ ±0.5°.
**User Auto-Calibration** compensates for *our* magnet + mounting tolerances, improving INL to
**≈ ±0.1°** — smoother commutation, less injected flutter (the design log's reason for picking
this part). One-time, at commissioning.

## Constraints

- **EEPROM endurance is only 1,000 erase/program cycles** → calibrate **once**, don't loop it.
- Requires a **constant rotation speed** within a selected range, for **>18 revolutions**.
- We trigger it **in software** (write `0x5E` to register `0x155`) — **no extra GPIO needed**,
  it all rides the SPI1 bus. `CAL_EN`→VDD is the hardware alternative (would need a dedicated
  GPIO); the module may not even expose the pin, and `CAL_EN` has an internal 250 kΩ pulldown
  so it's safe left unconnected (sits low = no calibration).

## AUTOCAL_FREQ band table (datasheet §10.2, verified)

| `AUTOCAL_FREQ[2:0]` (reg `0x00E[6:4]`) | Rotation speed (RPM) |
|---|---|
| 0x0 | 3200 ≤ s < 6400 |
| 0x1 | 1600 ≤ s < 3200 |
| 0x2 | 800 ≤ s < 1600 |
| 0x3 | 400 ≤ s < 800 |
| 0x4 | 200 ≤ s < 400 |
| **0x5** | **100 ≤ s < 200** |
| 0x6 | 50 ≤ s < 100 |
| 0x7 | 25 ≤ s < 50 |

Our belt ratio is **3.75:1** (90 mm platter / 24 mm motor pulley), so at 33⅓ RPM platter the
**motor runs 125 RPM** → use **`AUTOCAL_FREQ = 0x5`** and calibrate at ~150 RPM (15.7 rad/s).
(The geometric cal is speed-independent; we pick the band nearest our operating point.)

## Procedure (firmware, at M2c — verified against §10.2 / §8.6)

1. **Set the speed range** — read-modify-write `AUTOCAL_FREQ[2:0]` = reg `0x00E[6:4]` = `0x5`
   (preserve the other bits).
2. **Spin** the motor at a steady speed in that band (we use closed-loop velocity at ~150 RPM).
3. **Start**: write `0x5E` to reg `0x155`. Keep spinning **>18 rounds**.
4. **Poll status** — reg `0x113[7:6]`: `00`=none, `01`=running, `10`=failed, `11`=success.
   (PWM-out duty mirrors it: 50% running, 25% failed, >99% success — we read via SPI instead.)
5. **On success**: the datasheet says **power-down the chip** (steps 5–6: "other operations
   cannot be performed until power-on again"). Auto-cal **commits to EEPROM itself** — there is
   **no separate Program EEPROM step** in the cal flow. So: stop driving, then **power-cycle**.
   Each successful run consumes one EEPROM cycle (of ~1000) — calibrate once.
6. If failed, write `0x00` to `0x155` to exit, check mounting/air-gap/speed, and retry (a failed
   run does not persist, so it costs no EEPROM cycle).

> **Program EEPROM (cmd `1100`)** — verified framing for the *separate* zero-position use case
> (§9.1, §8.6.6): MOSI `{0xC0, 0x00, 0x00}` (cmd + all-zero address), device returns **ack `0x55`**
> on the 3rd byte; then poll **`EE_DONE` = reg `0x112[5]` = 1** before power-off. *Not* used by
> auto-cal. Implemented as `mt6826s::program_eeprom()` for later.

## Driver work this needs (add at M2c)

The M2a driver only does burst-read. Auto-cal additionally needs:
- **Register read** (cmd `0011`): `{0x30|A[11:8], A[7:0], 0x00}` → data on 3rd byte.
- **Register write** (cmd `0110`): `{0x60|A[11:8], A[7:0], data}`.
- **Program EEPROM** (cmd `1100`) if step 5 needs it.

## Related registers worth knowing

- **`ROT_DIR`** (reg `0x00D`) — rotation direction (0=CCW); affects SPI angle direction too. Set so
  increasing angle = forward platter rotation, to simplify the FOC sign convention.
- **`ZERO_POS[11:0]`** (regs `0x009/0x00A`) — zero-offset; can align electrical zero if useful.
- **Angle-read `STATUS[2:0]`** (reg `0x005`): bit0 over-speed, bit1 weak-field, bit2 under-voltage —
  already surfaced by `mt6826s_read()`; watch the weak-field bit during magnet mounting.

Datasheet: MT6826S Rev 1.1 (MagnTek), §8.6 (SPI), §10 (Calibration), §11 (Register Map).
