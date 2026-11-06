#include "contour_detector.hpp"

#include <opencv2/imgproc.hpp>

ContourDetector::ContourDetector(int min_area)
    : min_area_(min_area)
{
}

const std::vector<cv::Rect>& ContourDetector::detect(const cv::Mat& mask)
{
    cv::findContours(mask, contours_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // clear() keeps the vector's capacity, so after a few frames this stops allocating.
    boxes_.clear();
    for (const auto& contour : contours_)
    {
        const cv::Rect box = cv::boundingRect(contour);
        if (box.area() >= min_area_)
        {
            boxes_.push_back(box);
        }
    }
    return boxes_;
}
