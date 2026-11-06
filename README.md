# Sparse Frame Processing for Motion-Triggered Embedded Vision

Event-camera-inspired motion detection on a microcontroller: process only the regions of a
conventional camera frame that changed, and measure what that buys in speed, memory and
power on an STM32H7, and what it costs in detection accuracy.

> **Status: early research prototype.** The algorithm is being validated on PC against a
> public benchmark. Firmware is at toolchain bring-up. Results below are planned, not yet measured.

---

## Research question

Event cameras report only per-pixel brightness changes, so downstream processing scales with
scene activity instead of frame size. Conventional cameras don't work that way, but the idea
can be approximated in software: difference consecutive frames, keep only the regions that
changed, and process just those.

**Can this sparse processing reduce compute enough for real-time motion detection on
MCU-class hardware, and what are the accuracy, latency, memory and power trade-offs compared
with full-frame processing?**

### Framing

- **Resolution sets a fixed cost per frame.** Frame differencing, thresholding and
  morphological cleanup each touch every pixel, regardless of how much moved.
- **Sparsity saves work after that point:** ROI extraction, classification, encoding and
  transmission scale with the number and size of changed regions.
- An open hypothesis, to be tested rather than assumed: for motion detection (as opposed to
  fine recognition), low resolution may lose little accuracy relative to the compute it saves.
  TinyML person-detection models running on 96×96 grayscale input suggest coarse input can be enough.

## Approach

### Pipeline

```
camera ──► Y (luma) plane ──► |Yₜ − Yₜ₋₁| ──► threshold ──► open ──► close ──► ROI extraction ──► decision
           (8 bit/pixel)       absdiff         binary       noise    bridge     contours or grid
                                                mask        removal  fragments
```

Only the luminance plane is used. Differencing does not need colour, and dropping chroma
halves the per-frame memory compared with YUV422.

ROI strategies compared:

| Strategy | Method | Notes |
|---|---|---|
| **Contours** | external contours → bounding boxes, minimum-area filter | Tight boxes, but can fragment one object into many, because differencing finds edges and texture, not uniform interiors. Cost grows with mask noise. |
| **Projections** | recursive X-Y cut: count motion pixels per column, split into active intervals, then per row within each strip, alternating until regions stop splitting | About one pass per level and < 2 KB of memory. Fewer, larger boxes; objects that no straight cut separates share a box. |
| **Grid** | split mask into fixed cells, mark a cell active if its changed-pixel fraction exceeds a threshold | Coarse and MCU-friendly. Tried in the first prototype; not yet in the evaluation harness. |

Cost and memory of every stage, with PC measurements, are documented in
[docs/processing_profiles.md](docs/processing_profiles.md).

### Experiment design

Results will form a **2-D trade-off surface** over:

- **Sparsity:** difference threshold, morphology kernel sizes, grid cell size and fill threshold
- **Resolution:** QQVGA / QVGA / VGA (higher tiers are limited by on-chip RAM; see below)

Measured on each configuration:

| Metric | Notes |
|---|---|
| Throughput | frames per second |
| Latency | capture to decision |
| Memory | static and peak RAM footprint |
| Power | idle / active / peak |
| Accuracy | against CDnet 2014 ground truth, at pixel, grid-cell and frame (motion yes/no) level |

All measurements are repeated and reported as mean ± variance.

### Evaluation data

PC-side validation uses the public [CDnet 2014](http://changedetection.net/) change-detection
benchmark, which has pixel-level ground truth. See [data/README.md](data/README.md) for download
and layout. Native resolutions range from 320×240 to 720×576, so the resolution axis on PC is
produced by downsampling.

## Hardware

| Part | Role |
|---|---|
| [NUCLEO-H753ZI](https://www.st.com/en/evaluation-tools/nucleo-h753zi.html) | Cortex-M7 @ 480 MHz, 2 MB flash, ~1 MB SRAM, DCMI camera interface, hardware JPEG codec |
| OV7670 | VGA camera for interface bring-up |
| OV2640 | 2 MP DVP camera (raw/YUV output) for the resolution sweep |

The board has no external SDRAM, so on-chip SRAM limits resolution. That SRAM is split
across several non-contiguous banks (largest: 512 KB AXI SRAM), so storing two full luma
frames is tight at VGA and does not fit at SVGA. Higher tiers would need a different
frame-buffer strategy, which will be documented as such.

## Repository layout

```
.
├── pc-prototype/       Algorithm prototype — C++20, OpenCV, native build
├── firmware/           STM32H753 firmware — C++20, arm-none-eabi-gcc, CMake
│   └── cmake/          Cross-compilation toolchain file
├── data/               Datasets (git-ignored; see data/README.md)
├── docs/
│   ├── NOTES.md                Running decision log
│   └── processing_profiles.md  Cost, memory and accuracy of each technique
└── .github/workflows/  CI: builds both targets on every push
```

The two targets are separate CMake projects because they use different toolchains.

## Building

Developed on Ubuntu (WSL2). Requirements: CMake ≥ 3.16, a C++20 compiler and OpenCV 4.

**PC prototype**

```bash
sudo apt install build-essential cmake libopencv-dev
cmake -S pc-prototype -B pc-prototype/build
cmake --build pc-prototype/build
./pc-prototype/build/vision_prototype data/baseline/highway data/baseline/office
```

Each argument is a CDnet video folder. For every mask stage (threshold, +open, +close) and
every ROI method on each stage, the program prints pixel-level accuracy, frame coverage,
boxes per frame and processing time per frame, followed by a per-stage timing profile.
Run it without arguments to list the options (thresholds, kernel sizes, projection
parameters, `--per-video`, `--show`).

**Firmware**

Toolchain stub only; board bring-up pending. Build instructions will follow once the
board runs.

## Roadmap

**PC prototype**
- [x] Frame differencing → threshold → open/close pipeline
- [x] Contour-based ROI extraction
- [x] Projection (recursive X-Y cut) ROI extraction
- [x] Evaluation harness against CDnet 2014 ground truth (pixel level), with per-stage timing
- [ ] Grid-based ROI extraction in the harness
- [ ] Object-level hit rate
- [ ] Merge fragmented contour boxes into per-object ROIs (cheap clustering, e.g. union-find on overlapping boxes)
- [ ] Quantitative contour-vs-grid comparison (sparsity ratio, frame-to-frame ROI stability)
- [ ] Parameter and resolution sweeps

**Firmware**
- [x] CMake cross-compilation toolchain for Cortex-M7
- [ ] CubeMX startup code and linker script, board bring-up (LED, UART)
- [ ] SCCB camera configuration, DCMI + DMA luma-only frame capture
- [ ] Port the validated pipeline to the MCU
- [ ] Profiling: FPS, latency, memory, power
- [ ] Unit tests (Unity)

**Later**
- [ ] Use the motion trigger to drive LoRaWAN / MQTT uplink
- [ ] Write-up as a short paper

## Known limitations

Frame differencing is simple by design, and it has well-documented weaknesses that this
project reports rather than hides:

- **Sensitive to noise and lighting changes**, which the morphological opening only partly suppresses
- **Misses slow motion**, where the change between consecutive frames falls below the threshold
- **Leaves holes inside objects**, because uniform interiors produce little difference signal
- **Misses objects that stop moving**, since a static object produces no difference
- **HAL overhead**: firmware will use the STM32 HAL, so timings will include driver overhead that direct register access would avoid

## Related work

- Sehairi, Chouireb & Meunier, *Comparative study of motion detection methods for video surveillance systems* — [arXiv:1804.05459](https://arxiv.org/abs/1804.05459)
- NeuroHSMD: neuromorphic hybrid spiking motion detector — [arXiv:2112.06102](https://arxiv.org/abs/2112.06102)
- Wang et al., *CDnet 2014: An Expanded Change Detection Benchmark Dataset*, CVPRW 2014 — [paper](https://openaccess.thecvf.com/content_cvpr_workshops_2014/W12/html/Wang_CDnet_2014_An_2014_CVPR_paper.html)
