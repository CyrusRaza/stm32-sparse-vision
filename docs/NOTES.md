# Decision log

Decisions made in this project and the reasoning behind them, newest topics last. Measured
results live in [processing_profiles.md](processing_profiles.md).

---

## Research design

- **Goal: motion-gated ROIs for a downstream detector.** Cheap frame differencing finds what
  moved; ROIs extracted from the motion mask are passed to a detector (e.g. person or face)
  instead of the full frame. The project measures processing savings against detection
  accuracy.
- **Why not an event camera:** event cameras give no intensity image, so standard detectors
  don't apply directly, and the sensors are expensive. A conventional camera plus software
  differencing keeps pixel images for the detector.
- **Stopped and static objects are out of scope.** No motion means no ROI. This is stated as
  a limitation of the approach, not treated as a failure to fix.
- **Cheap methods are deliberate.** Background models such as ViBe or MOG2 and optical flow
  were considered and rejected for the motion stage: their per-pixel memory and compute
  conflict with the MCU budget the project is testing.
- **The detector drives total cost.** A fixed-input detector costs the same per run, so the
  number of ROIs matters more than their area. ROI extraction should favour recall: a merged
  box covering two objects is acceptable, a missed object is not.
- **Resolution as a second axis.** Resolution raises the fixed per-frame cost; sparsity saves
  work on top of it. Whether a low-resolution motion map loses ROIs is a hypothesis to test,
  not assume. TinyML person-detection models run at 96×96 grayscale, which suggests coarse
  input may be enough for this stage.
- **Luma only.** Differencing needs only the Y channel. Keeping Y from YUV422 halves the
  frame memory compared with full YUV, which raises the resolution ceiling for the same RAM.

## Evaluation

- **Dataset: CDnet 2014, baseline category**, instead of self-recorded webcam footage. It is
  public, has pixel-level ground truth, and avoids WSL2 USB passthrough for video devices.
- **Scoring follows the CDnet label rules:** 255 is motion; 0 (static) and 50 (shadow) are
  negative; 85 (outside ROI) and 170 (unknown) are ignored, as are pixels outside `ROI.bmp`.
  Only frames inside `temporalROI.txt` are scored. Confusion counts are summed per video;
  the category mean is the mean of per-video metrics.
- **ROI boxes are scored by filling them into a mask** and scoring that per pixel. This is
  a project-specific choice, not comparable with published CDnet results, and it
  penalises boxes that are larger than the object. Object-level hit rate is planned to
  complement it.
- **Timing:** per-stage wall time with an RAII scoped timer, OpenCV forced single-threaded.
  PC timings are relative only, because OpenCV uses SIMD and the projection code is scalar.
  They will be replaced by cycle counts (DWT) on the MCU.

## ROI extraction

- **Contours** (external contours → bounding boxes) give tight boxes but fragment objects,
  and their cost grows sharply with mask noise.
- **Projections (recursive X-Y cut)** were added as a cheaper alternative: column sums split
  the frame into strips, row sums split each strip, alternating until a region stops
  splitting on both axes. Gap merging along each axis acts as a 1-D close, which makes the
  2-D close redundant before this method. Objects that no straight cut separates share a
  box, which is acceptable for the detector use case.
- **Grid cells** were tried in the first prototype and are not yet in the harness.

## Hardware

- **Board: NUCLEO-H753ZI.** The H743ZI2 is discontinued; the H753 is pin-compatible with the
  same Cortex-M7 @ 480 MHz, ~1 MB SRAM and DCMI, plus a crypto/hash accelerator. It also has
  a hardware JPEG codec, relevant for higher resolutions without external memory.
- **No external SDRAM** on the Nucleo-144 board. FMC pins are broken out, but internal SRAM
  is the real resolution ceiling.
- **Cameras:** OV7670 (VGA, pin headers) for bring-up; OV2640 (2 MP, DVP, raw/YUV at all
  resolutions per the datasheet) for the resolution sweep. The OV2640 module needs a 24-pin
  0.5 mm FPC adapter; its pinout must be continuity-checked before wiring.
- **Moderate lens, not wide-angle.** Wide-angle distortion would be a confound in a
  resolution/sparsity study and adds nothing the algorithm needs.
- **No SBC in the loop.** Pairing with a Raspberry Pi or Jetson would undercut the
  "resource-constrained MCU" framing. A gateway that receives detections over LoRaWAN would
  be a separate downstream node.

## Toolchain

- **C++20** for both targets. The PC prototype uses C++ and OpenCV, not Python, so the
  algorithm maps directly onto the firmware port.
- **CMake + arm-none-eabi-gcc in WSL2**, not the STM32CubeIDE project format, so the local
  build command is the CI build command.
- **STM32CubeMX** generates pin configuration, clock tree, startup file and linker script.
  A from-scratch bare-metal setup is not a research variable here and adds schedule risk.
- **Firmware flags:** `-mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16
  -fno-exceptions -fno-rtti`. No exceptions or RTTI, to avoid unwind-table overhead.
- **No uncontrolled heap use in firmware.** Unpredictable heap growth would undermine the
  memory-footprint measurements.
- **Debugging risk:** forwarding the ST-LINK into WSL2 (`usbipd-win`) is known to be flaky.
  Fallback: CubeIDE on Windows for debug sessions only, with CMake as the source of truth.
- **Separate CMake projects** for `pc-prototype/` and `firmware/`, because they use
  different toolchains.
