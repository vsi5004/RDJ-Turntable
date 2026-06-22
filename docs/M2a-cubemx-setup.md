# Milestone 2a — CubeMX additions: SPI1 + MT6826S encoder (platter motor)

Brings up **SPI1** (full-duplex) to talk to the platter motor's **MT6826S** absolute encoder.
No motor/driver board needed yet — just the MT6826S on a diametric magnet. We read the
15-bit angle and stream it over RTT; rotate the magnet by hand and watch it track.

Open `RDJ-Turntable.ioc`, apply the following, **Generate Code**, then reply "generated".

## 1. SPI1 peripheral

**Connectivity → SPI1**:
- *Mode*: **Full-Duplex Master**  (we send a command and clock back the response → MISO needed)
- *Hardware NSS Signal*: **Disable**  (CS by GPIO)
- **Parameter Settings**:
  - *Data Size*: **8 Bits**
  - *First Bit*: **MSB First**
  - *Prescaler*: **8**  → 84 MHz APB2 / 8 = **10.5 MHz** (MT6826S max ~16 MHz; /4 = 21 MHz would overshoot)
  - *Clock Polarity (CPOL)*: **High**
  - *Clock Phase (CPHA)*: **2 Edge**    (CPOL=1/CPHA=1 = **SPI mode 3**, required by the MT6826S)

SPI1 should land on **PA5 (SCK), PA6 (MISO), PA7 (MOSI)** — confirm those three pins are the
ones assigned (they're free; no conflicts).

## 2. Chip-select GPIO

| Pin | Mode | User Label | GPIO settings |
|-----|------|-----------|---------------|
| PA4 | GPIO_Output | `MT_CS` | Output level **High** (deselected) |

(No DMA for M2a — the angle read is a 6-byte polled transfer. We'll add DMA later only if the
FOC ISR timing needs it.)

## 3. Generate

**GENERATE CODE**. My `app_init()`/`app_run()` hooks, `App/`, and `CMakeLists.txt` survive.
After it generates, `Core/Src/spi.c` will define `hspi1`, and `main.h` will define `MT_CS_Pin`.

## Wiring (MT6826S → board)

| MT6826S | Board |
|---------|-------|
| SCLK | PA5 |
| MISO (SDO/DO) | PA6 |
| MOSI (SDI/DI) | PA7 |
| CSN | PA4 |
| VCC | 3V3 |
| GND | GND |

Mount the **diametric magnet** centered over the chip per the datasheet's air-gap spec.

## After "generated"

I'll wire the `mt6826s` driver into `app.c` (read + RTT stream every 200 ms), add it to
`CMakeLists.txt`, build, and you flash. Expected RTT:

```
MT6826S raw=A0 03 .. .. .. ..  angle=NNNNN  DDD.D deg  st=0xNN  crc=OK
```

Rotate the magnet → `angle`/degrees track smoothly 0→360, and `crc=OK` confirms the protocol
is correct. If `crc=BAD`, the raw bytes tell us the alignment to fix.
