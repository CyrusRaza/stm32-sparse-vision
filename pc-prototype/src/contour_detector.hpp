#pragma once

#include <vector>

#include <opencv2/core.hpp>

// Turns a binary motion mask into bounding boxes of its external contours.
// Boxes smaller than min_area pixels are dropped as noise.
class ContourDetector
{
public:
    explicit ContourDetector(int min_area);

    // Returns the boxes found in mask. The reference stays valid until the
    // next call, which reuses the same storage.
    const std::vector<cv::Rect>& detect(const cv::Mat& mask);

private:
    int min_area_;
    std::vector<std::vector<cv::Point>> contours_;
    std::vector<cv::Rect> boxes_;
};
