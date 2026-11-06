# Project Log — Sparse Frame Processing for Embedded Vision

Running log of decisions, reasoning, and problems hit. Update as you go, not at the end — reconstructing "why" from memory later is harder than logging it now.

---

## Research design

- **Core research question** (unchanged from original plan): can event-inspired sparse frame processing reduce compute enough for real-time motion detection on MCU-class hardware, and what are the accuracy/latency/power trade-offs vs. full-frame processing?
- **Added a second axis: resolution.** Originally considered resolution vs. sparsity as competing concerns (more pixels = fixed differencing cost, diminishing the relative value of sparsity), but decided this is actually a hypothesis worth testing empirically rather than assuming. Revised framing for the paper: *resolution raises the fixed compute floor per frame; sparsity provides relative savings on top of that floor.* Plan: sweep sparsity threshold at multiple resolution tiers (QVGA/VGA/SVGA) and report the resulting 2D trade-off surface, not a single curve.
- Open hypothesis to test empirically, not assume: whether low resolution costs more in detection accuracy than the compute savings justify for this task. Precedent worth citing either way: TinyML person-detection reference models run at 96×96 grayscale and are considered standard for MCU-class deployment — suggests coarse resolution may be sufficient for motion/change detection specifically (not fine recognition).
- Luminance-only optimization: frame differencing only needs the Y channel, not full color — extracting just Y from YUV422 halves memory footprint per frame vs. storing full YUV/RGB, effectively doubling the resolution ceiling for the same RAM budget.

## Hardware

- **Board: NUCLEO-H753ZI** (not H743ZI2 — that variant is officially obsolete, discontinued by ST). H753 is a pin-compatible drop-in replacement: same Cortex-M7 @ 480MHz, same 1MB RAM/2MB flash, same DCMI interface, adds a hardware crypto/hash accelerator H743 lacked. Also has a hardware JPEG codec (relevant if pushing toward higher resolutions without external SDRAM).
- Nucleo-144 board does NOT ship with SDRAM populated — FMC pins are broken out on the Morpho connector if external memory is ever needed, but not populated by default. Internal usable SRAM (~864KB) is the real resolution ceiling without adding external memory.
- Actual price paid: ~€35 (not the ~€25-30 originally estimated from the obsolete board's old listing).
- **Primary camera: OV7670** — VGA max, real pin headers (no adapter needed), ~€5, DCMI-compatible. Good for initial bring-up.
- **Secondary camera: OV2640** (diymore, 160° wide-angle, confirmed DVP interface, 24-pin FPC connector) — 2MP, supports raw/YUV output at all resolution tiers per datasheet (not JPEG-locked, contrary to some ESP32-community folklore which is a host DMA/bandwidth limitation, not a sensor limitation). For the resolution-sweep experiments specifically.
  - Needs a separate **24-pin, 0.5mm pitch FPC-to-2.54mm DIP adapter** (~€6.50) — OV2640 has no direct pin-header variant on the market, this is inherent to the sensor package.
  - Actual total camera+adapter cost: ~€19.50 (camera ~€13 w/ shipping + adapter ~€6.50) — higher than originally estimated.
  - Open risk when adapter arrives: confirm contact-side orientation (top/bottom-facing) and verify pinout via continuity check before wiring to the Nucleo — don't trust generic adapter header labels blindly.
- FOV decision: went with a standard/moderate lens rather than paying extra for wide FOV. Reasoning: wide angle adds edge distortion (a confound for a controlled resolution/sparsity study) and doesn't affect the core algorithm, which only needs pixel intensity change, not undistorted geometry.
- **Total hardware cost, actual: ~€55** (board + both cameras + adapter), higher than the plan's original ~€30-40 estimate — driven mainly by the H743→H753 price jump and underestimating OV2640+adapter cost.
- Also needed, easy to forget budgeting for: jumper wires (Dupont, female-female + male-female, ~€4.40 for a 10cm set), a logic analyzer (~€10, strongly recommended for DCMI/SCCB debugging), USB data cable for ST-LINK.

## Hardware NOT chosen, and why

- Considered pairing with an SBC (Raspberry Pi 5, Jetson Orin Nano) to run a small LLM alongside the vision pipeline. Decided against folding this into the MCU project — conflicts with the "resource-constrained MCU hardware" research framing. If pursued, treat as a separate downstream node (MCU → LoRaWAN → LLM-capable gateway), not a replacement for the STM32.
- Considered STM32 alternatives (ESP32-S3, Teensy 4.1, RP2350) for general portfolio breadth — concluded the H753's peripheral spread (DCMI, Ethernet, CAN FD, crypto) already covers most of what those offer individually, so it's the strongest single "workhorse" board; the others are worth owning later as complementary boards for different project types, not replacements.

## Firmware toolchain

- **Language: C++, not C.** Rationale: portfolio already has C projects from prior work, none in C++; also chosen over re-doing a Python CV project despite thesis already using Python/OpenCV (redundant signal vs. closing an actual gap).
- **Build system: CMake + arm-none-eabi-gcc via WSL2**, not STM32CubeIDE's default Eclipse-makefile project. Rationale: matches the project plan's stated CI approach (GitHub Actions) exactly — local build command = CI build command, no "works on my machine" gap.
- Still using **STM32CubeMX** (standalone, not the full IDE) to generate pin config, clock tree, startup file (`startup_stm32h753xx.s`), and linker script — not hand-rolling these from scratch. Decided against a full from-scratch bare-metal build: not a research variable for this project, adds schedule risk, and the project plan already allows for FreeRTOS rather than pure bare-metal.
- Compiler flags settled on for the freestanding/embedded build: `-mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 -fno-exceptions -fno-rtti`. No exceptions/RTTI — standard embedded practice, avoids pulling in unwind-table overhead.
- Known friction accepted going in: USB passthrough (`usbipd-win`) for forwarding the ST-LINK debugger from Windows into WSL2 is a known flaky point. Fallback plan if it blocks debugging: use CubeIDE natively on Windows just for interactive debug sessions, keep CMake/CLI as the source of truth for the actual build.
- Heap allocation: avoid or tightly control dynamic allocation in firmware (STL containers that allocate, `new`/`delete`) given ~1MB RAM and the project's own goal of measuring memory footprint — unpredictable heap growth would undermine that measurement.

## PC-side prototype (pre-hardware validation)

- Separate project folder (`pc-prototype/`) from the firmware project (`firmware/`) — different toolchains (native g++ + OpenCV vs. arm-none-eabi-g++ + nostdlib), shouldn't share a CMakeLists.
- Language: C++ + OpenCV (not Python) — same reasoning as above, plus: the algorithm written here maps more directly onto the eventual firmware port than a Python version would.
- Testing against **recorded video files**, not live WSL2 webcam capture — more reproducible (committable test case), and avoids WSL2 USB-passthrough friction for video devices specifically.
- Status: basic capture/playback loop scaffolded (`pc-prototype/src/main.cpp`), not yet extended to actual frame-differencing logic.

## Open items / next steps

- [ ] Confirm recorded test video captures usable motion for the differencing algorithm
- [ ] Implement frame-differencing + sensitivity threshold + ROI extraction in `pc-prototype`
- [ ] Order/confirm FPC adapter contact-side orientation once it arrives; continuity-check pinout before powering the OV2640
- [ ] CubeMX: generate H753ZI startup file + linker script, read through it before wiring into CMake
- [ ] Re-upload thesis code to GitHub — original 500GB dataset (self-collected) was lost to storage constraints; repo will need to document this honestly rather than bundle data. Check what result artifacts (plots, metrics, weights) survived.
- [ ] Literature review: 2-3 papers on event-based optical flow / neuromorphic perception (per original plan's Step 3) — not yet started
- [ ] GitHub repo cleanup items from original plan (CI badge, getting-started section, fix "parcer" typo, etc.) — not yet started