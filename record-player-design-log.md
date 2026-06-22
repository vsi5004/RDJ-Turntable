# Custom Record Player — Design & Decision Log

> Context handoff document. Captures the architecture, decisions, rationale, key
> numbers, and open items for a from-scratch motorized hi-fi turntable build.
> Decisions marked **DECISION** are settled; please don't re-litigate without reason.

---

## 1. Project Overview

A fully custom, motorized high-fidelity turntable built from scratch — mechanical,
electrical, and embedded-software. Belt-driven platter under FOC motor control, a
motorized **linear-tracking** tonearm with servo-follow position correction, CAN-bus
connectivity, and a four-key screen-based HMI.

**System-level parameters**

- Supply: **24 VDC** system-wide. Motors and cueing servo on separate rails off 24 V.
- Comms: **CAN 2.0B classic** (not FD). **Single node** — the whole player is one node on a larger network.
- Records: **12-inch only** (33⅓ and 45 RPM; 45 still relevant for 12" singles / audiophile pressings).

---

## 2. Mechanical Architecture

### Platter drive
- Motor: **2204 260 KV gimbal motor** (high pole count, low cogging).
- Reduction: **8:1 belt drive**. Motor runs ~266 RPM for a 33⅓ platter.
- Platter: 3D-printed shell filled with **epoxy granite** for vibration damping + flywheel mass.
  - Watch: **balance** — a filled platter must be well-balanced or it produces once-per-rev wow/rumble.
- Reflected platter inertia at the motor is divided by 64 (1/N²) → eases motor control; the platter
  flywheel dominates short-term speed stability (good for flutter).

### Main bearing
- **Ceramic ball on sapphire thrust plate, oil-lubricated journal sleeve** (radial). Good high-end design.
- It is the dominant rumble source, so:
  - Journal **radial clearance + oil viscosity** set the rumble/wobble floor — keep tight and matched.
  - Ball-on-flat is a point contact under a heavy platter; materials are hard enough — keep clean and oiled.

### Tonearm (linear tracking)
- Carbon-fiber tube arm, **Ortofon 2M** cartridge.
- Motorized carriage: a second low-cogging gimbal motor driving a **9 mm linear bearing via capstan drive**.
- Position: high-resolution **SPI magnetic encoder** on the motor (doubles as carriage position via the capstan ratio).
- 3D-printed **flexure** connecting arm to carriage.
- **IR deflection-angle sensing** measures arm-vs-carriage deflection; the servo drives the carriage to null it
  → **servo-follow** architecture.
- Cueing: **mini servo** to lift/lower the stylus.

---

## 3. Control Architecture

### Platter speed control — the key challenge
- The belt is a torsional spring between two sensors → **two-mass resonant plant**
  (motor inertia / belt / platter inertia), resonance often a few Hz to low tens of Hz.
- **Commutate FOC on the motor's magnetic encoder** (rigidly coupled — correct).
- The **outer velocity loop on the platter ABI encoder must be LOW bandwidth (~1–5 Hz)**, below the belt
  resonance, to avoid exciting it. Fine for a turntable: the belt mechanically filters flutter and the
  flywheel handles short-term stability.
- This is **not** SimpleFOC's default single-sensor pattern — hand-roll the outer loop reading the platter
  encoder, commanding motor velocity/torque.
- Simpler alternative: close the speed loop on the motor encoder, use the platter ABI only as a periodic
  trim/monitor. Slightly worse belt-creep rejection, much easier to tune.

### Platter velocity estimation
- At 33⅓ RPM, the 1000 PPR ABI (4000 edges/rev quadrature) gives ~2222 edges/s.
- **Don't** count edges per fixed window (~2 counts/ms = garbage resolution).
- **Do** measure **period between edges** (M/T method); timestamp edges. Use the **index pulse** (once/rev)
  as a drift-free absolute RPM reference.

### Tonearm servo-follow — the other hard part (prototype first)
- **Frequency split:** servo handles slow inward drift + record eccentricity (~0.55 Hz and low harmonics);
  the flexure absorbs audio-band lateral motion.
- The thing to engineer is **servo bandwidth + IR sensor resolution/noise**, not a magic flexure stiffness.
- Keep the flexure soft enough that its lateral force on the stylus stays **sub-mN** (negligible vs ~18 mN VTF
  at 1.8 g), and rely on the servo to keep it near-centered so its low resonance is never driven hard.
- Make the flexure **swappable** for empirical tuning.
- Resonance check: **f ≈ 159 / √(M·C)**, M in grams, C in µm/mN.

---

## 4. Key Numbers

### Cartridge / effective mass
- Ortofon 2M: **~20 µm/mN** dynamic compliance, **~7.2 g**.
- Target arm/cartridge resonance **8–12 Hz** → total effective mass **~9–13 g** (10 Hz ≈ 12.6 g).
- Cartridge + hardware ≈ 8 g, so the **arm itself contributes only ~1–5 g effective** — a light arm.
  Carbon fiber + short length + minimal counterweight.
- Linear-arm subtlety: **vertical and lateral effective masses are separate**; both should land in 8–12 Hz.
  The flexure means lateral effective mass = arm inertia about the flexure, not the whole carriage — don't
  let stiff signal wire short that out.

### Wow & flutter
- Target: weighted (DIN/IEC) W&F below **~0.05%** is effectively inaudible (best direct drives ~0.02–0.025%).
- Reality: **record eccentricity** (off-center pressing hole) produces a 0.55 Hz wow of ~0.1–0.3% regardless
  of drive quality — the disc sets the floor. The goal is **"don't ADD audible flutter."**
- Ear is most sensitive **~4 Hz** — keep control-loop and motor artifacts out of the 1–10 Hz band.
- Gimbal cogging fundamental lands **~350–400 Hz** at running speed (LCM of slots/poles × motor rev rate) —
  above the flutter-sensitive band and filtered by belt + flywheel. Non-issue.

---

## 5. Electronics

### MCU — **DECISION: STM32F407VET6**
- Cortex-M4F @ 168 MHz, single-precision FPU, 512 KB flash / 192 KB RAM, **3× SPI**, **2× bxCAN (classic 2.0B)**, 100 pins.
- **168 MHz is plenty:** it's the standard FOC class (e.g. B-G431B-ESC1 is a 170 MHz STM32G4 running full
  current-sensed FOC). Dual FOC ≈ 25% of budget; FOC runs in a high-priority ISR so the GUI can't starve it.
  Gimbal motors in voltage mode need no current sensing → lighter still.
- **Why STM32 over RP2350:** built-in CAN (no external XL2515-over-SPI), hardware **timer encoder mode** for the
  platter quadrature (no PIO burn), and **3 SPI buses vs 2**. The call was about peripherals, not clock
  (168 MHz M4F ≈ 150–200 MHz M33 in throughput).

**MCU options evaluated**

| Part | Core | CAN | Verdict |
|------|------|-----|---------|
| STM32F103C6T6 | M3 @ 72 MHz, no FPU, 32 KB/10 KB | 1× bxCAN | Too small/FPU-less for the brain; only viable as a subordinate CAN node |
| **STM32F407VET6** | M4F @ 168 MHz, 512 KB/192 KB | 2× bxCAN | **CHOSEN** |
| STM32H723ZGT6 | M7 @ 550 MHz, DP FPU, 1 MB/564 KB | 2× FDCAN | Overkill-but-great; pick for GUI-heavy 4-screen work, the nicer 16-bit ADC for IR sensing, or raw headroom |
| STM32H743IIT6 | M7 @ 480 MHz, 2 MB/1 MB, 176 pins | 2× FDCAN | Most overkill; no edge over the H723 here |

> Note: H723/H743 are **single-core M7**. The dual-core H7s are H745/H747/H755/H757 (M7+M4) — not needed; a
> single M7 has ample headroom and dual-core adds complexity (two binaries, HSEM, cache domains).

**H7 vs F4 — real H7 advantages (mostly unneeded here)**
- Better ADC: **16-bit + hardware oversampling** (helps the noisy IR deflection null). *Most build-relevant.*
- More RAM + **Chrom-ART (DMA2D)** for GUI-heavy multi-screen work; the F407 has 192 KB and no DMA2D.
- Raw headroom (3–5× via superscalar M7 + cache).
- **Cost of H7:** multi-domain RAM + L1 cache → DMA needs cache maintenance/placement, FOC ISR code/data in
  TCM for deterministic timing. The F407's flat memory "just works."

### Motor drivers
- **SimpleFOC Mini** boards (one per motor). **TODO:** verify the driver's voltage rating covers the 24 V rail
  (gimbal motors are high-resistance/low-current; voltage-limit in SimpleFOC anyway).

### Encoders
- **Platter DRIVE MOTOR commutation — DECISION: MT6826S** (over a 600 CPR AB encoder).
  - MagnTek AMR magnetic **absolute, 15-bit (32,768 steps/rev) via SPI**, **INL < ±0.07°** after self-calibration,
    programmable ABZ/UVW/PWM, 3.3–5 V.
  - Why: **absolute angle at power-on** (smooth start, no alignment wiggle through the belt); ~14× finer
    resolution than 600 CPR (2400 quad) → smoother commutation / less injected flutter; compact (chip +
    diametric magnet on the rear shaft); sits on SPI1.
  - 600 CPR AB only wins on optical magnetic-field immunity — a solved problem with proper diametric-magnet
    mounting. The motor encoder's job is commutation only; platter speed comes from the separate platter ABI.
  - **TODO:** run the MT6826S self-calibration spin once and store the result (earns the ±0.07° / smooth commutation).
- **Platter shaft:** 1000 PPR ABI quadrature + index — RPM feedback (period-between-edges + index reference).
- **Tonearm motor — DECISION: AS5048A** (AMS magnetic absolute, **14-bit (16,384 steps/rev) via SPI**, max
  **10 MHz**, **SPI mode 1**, 16-bit frames with even parity, pipelined read). Commutation + carriage position
  (via the capstan ratio). A different part from the platter's MT6826S, so it lives on its **own SPI bus** —
  see SPI allocation below.

### CAN
- CAN 2.0B classic, single node. F407 bxCAN is on-chip; **add an external transceiver** (SN65HVD230 / TJA1051) —
  the controller is on-chip, the transceiver is not.

### Power chain
- 24 VDC bus. **Regulate 24→5 V (and →3.3 V) upstream** for logic; **do not feed 24 V into a dev board's onboard
  regulator.** (The Waveshare RP2350-CAN's onboard MP28164 is a 2.5–5.5 V battery buck-boost — would need
  regulated 5 V anyway.)
- Motors + cueing servo run from their own rails off 24 V.
- Watch: **four display backlights** can pull a few hundred mA off the 3.3 V rail at full brightness — size the
  regulator / PWM-dim the backlights.

---

## 6. SPI Bus Allocation (F407, 3× SPI)

| Bus | Devices | Notes |
|-----|---------|-------|
| **SPI1** | Drive-motor **MT6826S** encoder | Latency-critical, read every FOC loop, full-duplex; APB2 → /8 ≈ 10.5 MHz (dev max ~16 MHz, mode 3) |
| **SPI2** | Tonearm-motor **AS5048A** encoder | Own bus: AS5048A differs in SPI mode + max clock, so don't share SPI1; full-duplex; APB1 → /8 ≈ 5.25 MHz (dev max 10 MHz, mode 1) |
| **SPI3** | ScreenKey displays | DMA'd, best-effort, write-only (MOSI); shared MOSI/SCLK/DC/RST + per-screen CS |
| *(timer)* | Platter ABI quadrature | Hardware timer encoder mode (not SPI/PIO) |

> **Why two encoder buses (updated):** the platter (MT6826S) and tonearm (AS5048A) encoders use **different SPI
> modes and max clocks**. Sharing one bus would force per-transaction CPOL/CPHA + prescaler reconfiguration in
> the FOC hot path. SPI2 was the spare, so each encoder gets its own correctly-configured bus.

> **Rule:** keep displays **off** the encoder bus — display writes are bursty and can stall the bus for ms;
> encoders need deterministic low-latency reads.

---

## 7. HMI — 4× Waveshare 0.85" ScreenKey Modules

- Each: **128×128 IPS, ST7735, 4-wire SPI (write-only)**, mechanical key switch (~50 gf, 50 M-cycle life),
  per-module **KEY + DC + CS + SCLK + DIN + RST + PWM-backlight**, 3.3/5 V.
- Wiring: share MOSI/SCLK/DC/RST; **unique CS + KEY GPIO per module**; backlight PWM shared or per-key.
  ~13 GPIO total — trivial on the 100-pin F407.
- The keys are **self-labeling screens** → they can subsume the separate "diagnostic display"; each key shows
  its function + live state.

### Button assignments (context-sensitive, since each key is a screen)

| Mode | Key 1 | Key 2 | Key 3 | Key 4 |
|------|-------|-------|-------|-------|
| Idle | Play | 33/45 | Cue-down-here | Menu |
| Playing | Pause | Skip ▶ | Cue ▲ | Stop/Home |
| Menu | Speed trim | Re-home/Calibrate | Diagnostics | Back |

- Core functions: Play/Pause/Stop, 33/45 speed (show **measured** RPM, e.g. 33.34), Cue (servo lift/lower),
  Track skip/seek (carriage encoder → detect quiet inter-track bands or a per-record position map).
- **Automate (not buttons):** end-of-side detection (run-out groove's big pitch jump → auto-lift + return),
  auto-cue to lead-in on Play. "Repeat side" is the one end-of-side behavior worth a button.
- Use the screens live: speed key shows measured RPM; play key shows an arm-position progress bar (estimate
  time remaining from radius).

### Display software
- For 4 small status keys: a **lightweight stack (TFT_eSPI with DMA sprites, or Adafruit_GFX)**.
  LVGL is overkill unless the **menu** side grows into scrolling lists/transitions/a richer central screen
  (then prefer the H7).
- **Render trick:** one shared off-screen 128×128 buffer (32 KB), render → DMA-push to the selected display →
  reuse for the next (updated sequentially → need **one** buffer, not four). Flicker-free, non-blocking.
- Assets: **embed a compact icon+font set in flash** (fits easily in 512 KB). **SD card** only earns its place
  for album art / full-screen animations / swappable themes; if used, use **SDIO/SDMMC** (not SPI) and cache
  assets in RAM, not per-refresh streaming.

### Real-time pattern
- FOC loops in **high-priority timer ISR(s)**; CAN/GUI/buttons/housekeeping in the main loop. The ISR preempts
  everything → control timing is protected regardless of GUI activity. Cleaner than splitting across cores.

---

## 8. Open Items / To Revisit

- **Tonearm servo loop is the highest-risk subsystem — prototype it first** (flexure stiffness, IR sensor
  resolution/noise, servo bandwidth vs ~0.55 Hz eccentricity).
- **Flexure compliance:** still open; make it swappable and tune empirically.
- **Final effective-mass budget** for the arm (target total ~12 g; arm contributes ~1–5 g).
- **Platter balancing** procedure for the epoxy-granite fill.
- **Cueing servo** can buzz when holding — pick a quiet one or depower after the move.
- **LVGL** was flagged earlier as worth reconsidering for the HMI — current lean is a lightweight lib for the
  keys, LVGL only if the menu grows (and ideally on the H7).
- **Confirm SimpleFOC Mini voltage rating** at 24 V.

---

## 9. Component Reference

- Drive + tonearm motors: **2204 260 KV gimbal motors**
- Motor drivers: **SimpleFOC Mini**
- Motor encoders: **MT6826S** (MagnTek, 15-bit AMR absolute, SPI — *platter*, on SPI1) and **AS5048A** (AMS, 14-bit absolute, SPI — *tonearm*, on SPI2)
- Platter encoder: **1000 PPR ABI quadrature + index**
- Cartridge: **Ortofon 2M** (~20 µm/mN, ~7.2 g)
- MCU: **STM32F407VET6** (alt: STM32H723ZGT6 for headroom)
- CAN: **bxCAN + external transceiver** (SN65HVD230 / TJA1051), 2.0B classic, single node
- HMI: **4× Waveshare 0.85" ScreenKey** (128×128, ST7735, SPI)
- Software: **SimpleFOC** (motor control), STM32duino / CubeIDE, **TFT_eSPI** or **Adafruit_GFX** (displays)
