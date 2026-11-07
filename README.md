# Motion-Gated ROI Extraction for Edge AI on Microcontrollers

Event-camera-inspired sparse processing on a conventional camera: find what moved with cheap
frame differencing, cut the frame down to a few regions of interest (ROIs), and run a
detector (e.g. person or face) only on those regions instead of the full frame. The goal is
to measure how much processing this saves, and how much detection accuracy it costs, on an
STM32H7 microcontroller.

> **Status: early research prototype.** Motion detection and two ROI extraction methods are
> implemented and evaluated on PC against a public benchmark (first results below). The
> downstream detector is not yet integrated. Firmware is at toolchain bring-up.

---

## Research question

Event cameras report only per-pixel brightness changes, so downstream work scales with scene
activity instead of frame size. But they produce no intensity image, which makes standard
detectors hard to apply, and they are expensive. A conventional camera plus software
differencing can approximate the same sparsity while keeping full pixel images for the
detector.

**Can motion-gated ROI extraction reduce the cost of running a detector enough to make
MCU-class hardware a useful edge-AI device, and what does it cost in accuracy compared with
running the detector on every full frame?**

### Framing

- **The motion stage has a fixed cost per frame.** Differencing, thresholding and
  morphology touch every pixel regardless of how much moved, so they must be cheap.
- **The detector dominates the total cost.** A detector with a fixed input size costs the
  same per run, so the number of ROIs (detector runs), not their pixel area, drives the
  savings. Frames without motion cost no detector run at all.
- **ROI extraction has to favour recall.** A missed region is a missed detection; an
  oversized or merged box only costs some extra detector work. One box covering two nearby
  objects is acceptable, because the detector can still find both.
- An open hypothesis, to be tested rather than assumed: the motion map can run at a lower
  resolution than the detector input without losing ROIs. TinyML person-detection models
  run on 96×96 grayscale input, which suggests coarse input can be enough for this stage.

## Approach

### Pipeline

```
camera ──► Y (luma) ──► |Yₜ − Yₜ₋₁| ──► threshold ──► open ──► close ──► ROI extraction ──► detector on each ROI
           8 bit/px      absdiff         binary       remove   bridge     contours or        (planned)
                                         mask         specks   gaps       projections
```

Only the luminance plane is used. Differencing does not need colour, and dropping chroma
halves the per-frame memory compared with YUV422.

ROI strategies:

| Strategy | Status | Method | Notes |
|---|---|---|---|
| **Contours** | implemented, evaluated | external contours → bounding boxes, minimum-area filter | Tight boxes, but one object often fragments into several, because differencing finds edges and texture, not uniform interiors. Cost grows with mask noise. |
| **Projections** | implemented, evaluated | recursive X-Y cut: count motion pixels per column, split into active intervals, then per row within each strip, alternating until regions stop splitting | About one pass per level and < 2 KB of memory. Fewer, larger boxes; objects that no straight cut separates share a box. |
| **Grid** | planned | split the mask into fixed cells, mark a cell active if its changed-pixel fraction exceeds a threshold | Coarse and MCU-friendly. Tried in an early prototype; not yet in the evaluation harness. |

Cost and memory of every stage are documented in
[docs/processing_profiles.md](docs/processing_profiles.md).

### First results (PC)

CDnet 2014 baseline category (4 videos, 320×240), mean over videos. Boxes are filled into a
mask and scored per pixel against the ground truth. This is a project-specific evaluation,
not comparable with published CDnet results. Times are single-threaded PC wall time for the
whole pipeline up to that output and are relative only.

| Output | Recall | Precision | Boxes/frame | µs/frame |
|---|---|---|---|---|
| mask: threshold + open + close | 0.34 | 0.79 | – | 188 |
| contours on threshold + open + close | 0.61 | 0.59 | 5.9 | 220 |
| projections on threshold + open | 0.65 | 0.51 | 3.6 | 147 |

Projections reach higher recall with fewer boxes (fewer detector runs) at lower cost than
contours. Full tables, parameters and per-stage times are in the
[processing profiles](docs/processing_profiles.md).

### Experiment design

Results will form a trade-off surface over:

- **Motion parameters:** difference threshold, morphology kernel sizes, ROI method and its parameters
- **Motion-map resolution:** QQVGA / QVGA / VGA (higher tiers are limited by on-chip RAM; see below)
- **Detector input size**, independent of the motion-map resolution

Measured on each configuration:

| Metric | Notes |
|---|---|
| ROI quality | pixel-level recall/precision against CDnet ground truth (done); object-level hit rate (planned) |
| Detector work | ROIs per frame, frame coverage, fraction of frames that trigger the detector |
| Detection accuracy | detector on ROIs vs. detector on full frames |
| Throughput and latency | frames per second; capture to decision |
| Memory | static and peak RAM footprint |
| Power | idle / active / peak |

MCU measurements will be repeated and reported as mean ± variance.

### Evaluation data

PC-side validation uses the public [CDnet 2014](http://changedetection.net/) change-detection
benchmark, which has pixel-level ground truth. The baseline category (320×240) is used so
far. See [data/README.md](data/README.md) for download and layout. The resolution axis on PC
is produced by downsampling.

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
├── pc-prototype/       Algorithm prototype and evaluation harness | C++20, OpenCV
├── firmware/           STM32H753 firmware | C++20, arm-none-eabi-gcc, CMake
│   └── cmake/          Cross-compilation toolchain file
├── data/               Datasets (git-ignored; see data/README.md)
├── docs/
│   ├── NOTES.md                Decision log
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
- [ ] Object-level hit rate and frame-level trigger metrics
- [ ] Grid-based ROI extraction in the harness
- [ ] Merge fragmented contour boxes into per-object ROIs
- [ ] Parameter and resolution sweeps
- [ ] Run a detector on the ROIs and compare against full-frame detection

**Firmware**
- [x] CMake cross-compilation toolchain for Cortex-M7
- [ ] CubeMX startup code and linker script, board bring-up (LED, UART)
- [ ] SCCB camera configuration, DCMI + DMA luma-only frame capture
- [ ] Port the validated pipeline to the MCU
- [ ] Run a quantised detector on the ROIs
- [ ] Profiling: FPS, latency, memory, power

**Later**
- [ ] Use the detections to drive a LoRaWAN / MQTT uplink
- [ ] Write-up as a short paper

## Known limitations

Frame differencing is simple by design, and it has well-documented weaknesses that this
project reports rather than hides:

- **Misses objects that stop moving**, since a static object produces no difference. The
  detector is never triggered on them; this is a stated limitation of the approach.
- **Misses slow motion**, where the change between consecutive frames falls below the threshold
- **Leaves holes inside objects**, because uniform interiors produce little difference signal
- **Sensitive to noise and lighting changes**, which the morphological opening only partly suppresses
- **Pixel-level scoring of boxes** penalises boxes that are larger than the object, even
  though a detector tolerates them; object-level metrics are planned to complement it
- **HAL overhead**: firmware will use the STM32 HAL, so timings will include driver overhead that direct register access would avoid

## Related work

- Sehairi, Chouireb & Meunier, *Comparative study of motion detection methods for video surveillance systems* | [arXiv:1804.05459](https://arxiv.org/abs/1804.05459)
- NeuroHSMD: neuromorphic hybrid spiking motion detector | [arXiv:2112.06102](https://arxiv.org/abs/2112.06102)
- Wang et al., *CDnet 2014: An Expanded Change Detection Benchmark Dataset*, CVPRW 2014 | [paper](https://openaccess.thecvf.com/content_cvpr_workshops_2014/W12/html/Wang_CDnet_2014_An_2014_CVPR_paper.html)

## License

[MIT](LICENSE)
