# Milestone 2b — CubeMX additions: SPI2 + AS5048A encoder (tonearm motor)

Brings up **SPI2** (full-duplex) to talk to the tonearm motor's **AS5048A** absolute encoder.
Its own bus (not shared with the MT6826S on SPI1) because it differs in SPI mode and max
clock. No motor needed yet — read the 14-bit angle and stream it over RTT, rotate the
magnet by hand to verify.

Open `RDJ-Turntable.ioc`, apply the following, **Generate Code**, then reply "generated".

## 1. SPI2 peripheral

**Connectivity → SPI2**:
- *Mode*: **Full-Duplex Master**
- *Hardware NSS Signal*: **Disable** (CS by GPIO)
- **Parameter Settings**:
  - *Data Size*: **16 Bits**  (AS5048A frames are 16-bit; the value clocks out MSB-first as one word)
  - *First Bit*: **MSB First**
  - *Prescaler*: **8**  → 42 MHz APB1 / 8 = **5.25 MHz** (AS5048A max is 10 MHz; /4 = 10.5 would just exceed it)
  - *Clock Polarity (CPOL)*: **Low**
  - *Clock Phase (CPHA)*: **2 Edge**    (CPOL=0/CPHA=1 = **SPI mode 1**, required by the AS5048A)

SPI2 pins (as assigned): **PB10 (SCK), PC2 (MISO), PC3 (MOSI)** — all free, no conflicts.

## 2. Chip-select GPIO

| Pin | Mode | User Label | GPIO settings |
|-----|------|-----------|---------------|
| PB12 | GPIO_Output | `AS_CS` | Output level **High** (deselected) |

(No DMA for M2b — the angle read is two 16-bit polled transfers.)

## 3. Generate

**GENERATE CODE**. `App/`, `CMakeLists.txt`, and the `app_init/app_run` hooks survive.
After generating, `Core/Src/spi.c` defines `hspi2`, and `main.h` defines `AS_CS_Pin`.

## Wiring (AS5048A → board)

| AS5048A | Board |
|---------|-------|
| CLK | PB10 |
| MISO | PC2 |
| MOSI | PC3 |
| CSn | PB12 (AS_CS) |
| VDD5V | 3V3 (or 5V) |
| VDD3V | (3V op: short to VDD5V; 5V op: 10 µF to GND) |
| GND | GND |

> If powering at **3.3 V**, short **VDD3V to VDD5V**. If at **5 V**, leave VDD3V with a 10 µF
> cap to GND (per datasheet). The TEST pins: pin 5 to GND, pins 6-10 left open.
> Mount the diametric magnet centered over the package.

## After "generated"

I'll wire the `as5048a` driver into `app.c` (read + RTT stream), build, and you flash.
Expected RTT:

```
AS5048A raw=XXXX  angle=NNNNN  DDD.D deg  EF=0  parity=OK
```

Rotate the magnet → `angle` tracks 0→16383 (0→360°), `parity=OK`. The even-parity check is
the protocol validator (like the MT6826S CRC). `EF=1` flags a prior transmission error
(cleared by a read of reg 0x0001 — we can add that if it shows up).
