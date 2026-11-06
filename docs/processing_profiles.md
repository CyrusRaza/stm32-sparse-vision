# Processing profiles

Cost and memory profile of every stage in the PC prototype, as the reference for the MCU
port. Each stage is described by its passes over the image, its approximate work per pixel,
and its working memory. PC timings measured on CDnet 2014 then follow.

> **PC timings are relative only.** OpenCV functions (absdiff, threshold, morphology,
> findContours) use SIMD-optimised implementations, while the projection detector is plain
> scalar C++. That difference favours OpenCV. Absolute numbers will be replaced by
> cycle counts on the STM32H753 (DWT cycle counter).

## Notation

- W × H: motion-map resolution (320 × 240 for CDnet baseline; 640 × 480 = VGA)
- All image buffers are 8-bit, one byte per pixel
- "Per pixel" figures are rough operation counts, not cycles

## Stages

### 1. Frame differencing (common to every method)

`absdiff → threshold → store previous frame`

| | |
|---|---|
| Passes | 3 as implemented (absdiff, threshold, copy). Can be done as **1** pass: diff, compare, write mask and update previous frame together. |
| Work per pixel | ~2–3 ops (subtract, absolute value, compare); vectorisable on M7 with the DSP SIMD instructions |
| Memory | previous frame W·H, plus mask W·H (diff buffer W·H only when not fused). 320×240: 154 KB; VGA: 614 KB (fused) |
| Depends on scene? | No. A fixed cost per frame. |

### 2. Morphological opening (3×3) and closing (5×5)

| | |
|---|---|
| Passes | each operation = erosion + dilation; separable rectangular kernels make this 2 one-dimensional passes per erosion/dilation |
| Work per pixel | naive separable: 2·k compares per erosion/dilation, so ~32 compares for open 3 + close 5. Van Herk/Gil-Werman: ~3 per 1-D pass, independent of k |
| Memory | one output buffer W·H per operation as implemented; a streaming version needs only k line buffers (k·W) |
| Depends on scene? | No. A fixed cost per frame, and the most expensive part of the mask pipeline. |

### 3a. Contour boxes (`ContourDetector`)

`findContours (external) → boundingRect → min-area filter`

| | |
|---|---|
| Passes | 1 raster scan, plus border following around every blob |
| Work per pixel | scan ~1 op/pixel, plus work proportional to total contour length; heavily branched |
| Memory | OpenCV makes an internal copy of the mask (≥ W·H), plus contour point lists (8 B per stored point) and the box list |
| Depends on scene? | **Yes, strongly.** Cost grows with the number and length of contours, so it explodes on noisy masks (see measurements). |

### 3b. Projection X-Y cut (`ProjectionDetector`)

`column sums → active intervals → per-strip row sums → … (recursive) → boxes`

| | |
|---|---|
| Passes | 1 per recursion level, over the regions still being split; each pixel is visited at most once per level. Typically 2–4 levels. |
| Work per pixel | 1 add per level; interval search is O(W + H), negligible |
| Memory | one profile of max(W, H) counters per active level (int as implemented; uint16 is enough), plus interval and box lists. **< 2 KB at VGA** |
| Depends on scene? | Mildly. More separated regions means more, but smaller, sub-projections. |
| Parameters | `min_count` (min motion pixels for an active row/column), `max_gap` (merge runs separated by ≤ g empty bins; a 1-D close), `min_area` |

## Measurements (PC)

Date: 2026-11-06. CDnet 2014 baseline (highway, office, pedestrians, PETS2006), 320×240,
4413 scored frames. Parameters: threshold 10, open 3×3, close 5×5, min area 10, projection
min count 3, max gap 4. Single-threaded (`cv::setNumThreads(0)`), Release build, WSL2.
Excludes image loading and scoring.

Reproduce:

```bash
./pc-prototype/build/vision_prototype --per-video \
    data/baseline/highway data/baseline/office data/baseline/pedestrians data/baseline/PETS2006
```

### Per stage

| Stage | µs / call |
|---|---|
| absdiff | 29.0 |
| threshold | 18.1 |
| store previous frame | 8.5 |
| open 3×3 | 69.5 |
| close 5×5 | 62.6 |
| contours on threshold mask | 317.3 |
| contours on threshold+open | 57.3 |
| contours on threshold+open+close | 32.7 |
| projections on threshold mask | 51.0 |
| projections on threshold+open | 21.6 |
| projections on threshold+open+close | 21.0 |

### Accuracy vs. cost per method

Accuracy: pixel-level, mean of per-video metrics. Box methods are scored on their filled
boxes. Cost: total pipeline time per frame up to and including that method.

| Method | Recall | Precision | F | Coverage | Boxes/frame | µs/frame |
|---|---|---|---|---|---|---|
| mask: threshold | 0.387 | 0.432 | 0.363 | 0.035 | – | 55.7 |
| mask: threshold+open | 0.262 | 0.761 | 0.352 | 0.015 | – | 125.1 |
| mask: threshold+open+close | 0.336 | 0.786 | 0.417 | 0.018 | – | 187.7 |
| contours on threshold | 0.751 | 0.427 | 0.500 | 0.070 | 40.4 | 373.0 |
| contours on threshold+open | 0.521 | 0.643 | 0.486 | 0.028 | 12.7 | 182.4 |
| contours on threshold+open+close | 0.605 | 0.592 | 0.499 | 0.037 | 5.9 | 220.4 |
| projections on threshold | 0.822 | 0.258 | 0.352 | 0.168 | 9.2 | 106.7 |
| projections on threshold+open | 0.647 | 0.505 | 0.475 | 0.049 | 3.6 | 146.8 |
| projections on threshold+open+close | 0.647 | 0.505 | 0.475 | 0.049 | 3.7 | 208.6 |

### Observations

- **Morphology dominates the mask cost.** Open and close together take ~132 µs of the
  ~188 µs full mask pipeline.
- **Contour cost depends on the scene.** On the raw threshold mask (noisy), contour
  tracing takes 317 µs, 10× its cost on the cleaned mask, and yields ~40 boxes per frame.
- **Projections are cheap and steady.** 21–51 µs, and only 2.4× more on the noisy mask.
- **Closing is redundant before projections.** Projection results with and without the
  close are identical, because gap merging already performs a 1-D close. The best
  projection pipeline therefore skips it: threshold + open + projections, 147 µs.
- **Projections on threshold+open vs. contours on the full pipeline:** about 33% less time
  (147 vs. 220 µs), ~40% fewer boxes (3.6 vs. 5.9, so fewer detector runs), higher recall
  (0.65 vs. 0.61) and lower precision (0.51 vs. 0.59). Projection boxes are larger and
  merge objects that no straight cut separates, which is acceptable when a detector runs on
  each box.
- **Projections on the raw threshold mask over-cover** (17% of the frame, 40% on highway):
  scattered noise accumulates along whole rows and columns. A light opening, or a
  `min_count` that scales with region size, is needed.

## Open points

- Replace PC timings with MCU cycle counts (DWT CYCCNT) for every stage.
- Fused single-pass differencing; streaming morphology with line buffers.
- Sweep `min_count` / `max_gap`; try a relative `min_count`.
- Object-level hit rate, to complement pixel-level scores.
