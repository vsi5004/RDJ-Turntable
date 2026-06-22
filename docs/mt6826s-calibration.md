# MT6826S — User Auto-Calibration (platter motor encoder)

From the MT6826S datasheet Rev 1.1, §10.2. Captured so we run it correctly at **M2c** (needs
the motor spinning). Not needed for M2a (raw angle read).

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

## Procedure (firmware, at M2c)

1. **Set the speed range** — `AUTOCAL_FREQ[2:0]` = reg `0x00E[6:4]` (read-modify-write; preserve
   the other bits, some are "MagnTek use only"). Our platter motor runs ~266–360 RPM (8:1 to the
   platter at 33⅓–45), which is the **200–400 RPM** band → **`AUTOCAL_FREQ = 0x4`**.
2. **Spin** the motor open-loop at a steady speed in that band.
3. **Start**: write `0x5E` to reg `0x155`. Keep spinning >18 rounds.
4. **Poll status** — reg `0x113[7:6]`: `00`=none, `01`=running, `10`=failed, `11`=success.
   (Or watch the PWM-out duty: 50% running, 25% failed, >99% success.)
5. **On success**: write `0x00` to `0x155` to exit auto-cal. ⚠️ **Verify** whether the coefficients
   auto-persist or need an explicit **Program EEPROM** (cmd `1100`, addr `0`, expect ack `0x55`,
   then wait `EE_DONE` = reg `0x112[5]` = 1 before power-off). The datasheet's cal section says
   "power down on success" but is ambiguous about the EEPROM step — confirm on the bench.
6. If failed, check mounting/air-gap/speed and retry.

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
