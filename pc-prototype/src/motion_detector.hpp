#pragma once

#include "profiler.hpp"

#include <opencv2/core.hpp>

struct MotionParams
{
    int threshold = 10;     // min |Y_t - Y_t-1| for a pixel to count as changed
    int open_kernel = 3;    // square kernel side; removes isolated noise pixels
    int close_kernel = 5;   // square kernel side; bridges fragments of one object
};

// Frame differencing on luma: |Y_t - Y_t-1| -> threshold -> open -> close.
class MotionDetector
{
public:
    explicit MotionDetector(const MotionParams& params);

    // Times each stage into the given profiler. Pass nullptr to stop profiling.
    void set_profiler(Profiler* profiler);

    // Feeds one 8-bit grayscale frame. Writes the binary motion mask (0/255)
    // and returns true, or returns false for the first frame, which only
    // primes the previous-frame buffer.
    bool process(const cv::Mat& gray, cv::Mat& mask);

    // Intermediate masks from the last process() call, kept for evaluation.
    // (The firmware version will do these steps in place instead.)
    const cv::Mat& thresholded() const { return thresholded_; }
    const cv::Mat& opened() const { return opened_; }

private:
    MotionParams params_;
    cv::Mat open_kernel_;
    cv::Mat close_kernel_;
    cv::Mat prev_;
    cv::Mat diff_;
    cv::Mat thresholded_;
    cv::Mat opened_;

    StageTime* t_absdiff_ = nullptr;
    StageTime* t_threshold_ = nullptr;
    StageTime* t_open_ = nullptr;
    StageTime* t_close_ = nullptr;
    StageTime* t_store_prev_ = nullptr;
};
