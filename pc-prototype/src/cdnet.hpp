#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <opencv2/core.hpp>

// Pixel-level confusion counts using the CDnet 2014 rules:
//   GT 255 = motion (positive), 0 = static and 50 = hard shadow (negative),
//   85 = outside ROI and 170 = unknown motion (ignored).
// Pixels outside the video's ROI.bmp are ignored as well.
struct Confusion
{
    std::int64_t tp = 0;
    std::int64_t fp = 0;
    std::int64_t fn = 0;
    std::int64_t tn = 0;

    void add(const cv::Mat& pred, const cv::Mat& gt, const cv::Mat& roi);

    double recall() const;
    double precision() const;
    double f_measure() const;
    double fpr() const;     // false positive rate
    double fnr() const;     // false negative rate
    double pwc() const;     // percentage of wrong classifications
};

// One CDnet video folder: input/, groundtruth/, ROI.bmp, temporalROI.txt.
class CdnetVideo
{
public:
    // Throws std::runtime_error if the folder is not a valid CDnet video.
    explicit CdnetVideo(std::filesystem::path dir);

    std::string name() const { return dir_.filename().string(); }
    int first_frame() const { return first_; }  // first scored frame (1-based)
    int last_frame() const { return last_; }    // last scored frame (1-based)
    const cv::Mat& roi() const { return roi_; }

    cv::Mat input(int frame) const;         // 8-bit grayscale
    cv::Mat groundtruth(int frame) const;   // 8-bit label image

private:
    std::filesystem::path dir_;
    cv::Mat roi_;
    int first_ = 0;
    int last_ = 0;
};
