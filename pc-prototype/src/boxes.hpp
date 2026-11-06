#pragma once

#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

// Paints boxes as filled 255 rectangles on a zeroed 8-bit mask of the given
// size, so ROI output can be scored like a pixel mask.
inline void render_boxes(const std::vector<cv::Rect>& boxes, cv::Size size, cv::Mat& out)
{
    out.create(size, CV_8UC1);
    out.setTo(0);
    for (const cv::Rect& box : boxes)
    {
        cv::rectangle(out, box, cv::Scalar(255), cv::FILLED);
    }
}
