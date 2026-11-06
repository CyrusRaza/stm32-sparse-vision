#include "motion_detector.hpp"

#include <opencv2/imgproc.hpp>

namespace
{

cv::Mat square_kernel(int side)
{
    return cv::getStructuringElement(cv::MORPH_RECT, cv::Size(side, side));
}

} // namespace

MotionDetector::MotionDetector(const MotionParams& params)
    : params_(params),
      open_kernel_(square_kernel(params.open_kernel)),
      close_kernel_(square_kernel(params.close_kernel))
{
}

void MotionDetector::set_profiler(Profiler* profiler)
{
    t_absdiff_ = profiler ? profiler->stage("absdiff") : nullptr;
    t_threshold_ = profiler ? profiler->stage("threshold") : nullptr;
    t_open_ = profiler ? profiler->stage("open") : nullptr;
    t_close_ = profiler ? profiler->stage("close") : nullptr;
    t_store_prev_ = profiler ? profiler->stage("store prev frame") : nullptr;
}

bool MotionDetector::process(const cv::Mat& gray, cv::Mat& mask)
{
    if (prev_.empty())
    {
        gray.copyTo(prev_);
        return false;
    }

    {
        ScopedTimer t(t_absdiff_);
        cv::absdiff(gray, prev_, diff_);
    }
    {
        ScopedTimer t(t_threshold_);
        cv::threshold(diff_, thresholded_, params_.threshold, 255, cv::THRESH_BINARY);
    }
    {
        ScopedTimer t(t_open_);
        cv::morphologyEx(thresholded_, opened_, cv::MORPH_OPEN, open_kernel_);
    }
    {
        ScopedTimer t(t_close_);
        cv::morphologyEx(opened_, mask, cv::MORPH_CLOSE, close_kernel_);
    }
    {
        // copyTo reuses prev_'s buffer after the first frame; clone() would allocate every frame.
        ScopedTimer t(t_store_prev_);
        gray.copyTo(prev_);
    }
    return true;
}
