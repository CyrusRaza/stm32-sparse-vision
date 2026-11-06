# Data

Datasets are not committed. This folder is git-ignored except for this file.

## CDnet 2014

Evaluation uses the [CDnet 2014](http://changedetection.net/) change-detection benchmark
(Wang et al., CVPRW 2014). No registration is required.

| What | URL |
|---|---|
| Full dataset (~3.9 GB) | http://changedetection.net/static/dataset/dataset2014.zip |
| Single category | `http://changedetection.net/static/dataset/<category>.zip` |
| Single video | `http://changedetection.net/static/dataset/<category>/<video>.zip` |

Extract so that each video sits at `data/<category>/<video>/`, for example:

```
data/
└── baseline/
    └── highway/
        ├── input/          in000001.jpg ...
        ├── groundtruth/    gt000001.png ...
        ├── ROI.bmp         spatial evaluation mask
        └── temporalROI.txt first and last evaluated frame
```

Ground-truth label values: `0` static, `50` hard shadow, `85` outside ROI,
`170` unknown motion, `255` motion.

Note: for the five categories added in 2014 (badWeather, lowFramerate, nightVideos,
PTZ, turbulence), ground truth is public only for the first half of each video.
