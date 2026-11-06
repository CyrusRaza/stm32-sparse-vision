#include "cdnet.hpp"

#include <format>
#include <fstream>
#include <stdexcept>
#include <utility>

#include <opencv2/imgcodecs.hpp>

namespace
{

constexpr uchar GT_STATIC = 0;
constexpr uchar GT_SHADOW = 50;
constexpr uchar GT_OUTSIDE_ROI = 85;
constexpr uchar GT_UNKNOWN = 170;
constexpr uchar GT_MOTION = 255;

// Returns 0 instead of dividing by zero (e.g. precision when nothing was detected).
double ratio(std::int64_t num, std::int64_t den)
{
    return den == 0 ? 0.0 : static_cast<double>(num) / static_cast<double>(den);
}

cv::Mat read_gray(const std::filesystem::path& path)
{
    cv::Mat img = cv::imread(path.string(), cv::IMREAD_GRAYSCALE);
    if (img.empty())
    {
        throw std::runtime_error("cannot read " + path.string());
    }
    return img;
}

} // namespace

void Confusion::add(const cv::Mat& pred, const cv::Mat& gt, const cv::Mat& roi)
{
    CV_Assert(pred.size() == gt.size() && gt.size() == roi.size());
    CV_Assert(pred.type() == CV_8UC1 && gt.type() == CV_8UC1 && roi.type() == CV_8UC1);

    for (int y = 0; y < gt.rows; ++y)
    {
        const uchar* p = pred.ptr<uchar>(y);
        const uchar* g = gt.ptr<uchar>(y);
        const uchar* r = roi.ptr<uchar>(y);

        for (int x = 0; x < gt.cols; ++x)
        {
            if (r[x] == 0 || g[x] == GT_OUTSIDE_ROI || g[x] == GT_UNKNOWN)
            {
                continue;
            }
            if (g[x] != GT_MOTION && g[x] != GT_STATIC && g[x] != GT_SHADOW)
            {
                throw std::runtime_error(std::format("unexpected ground-truth label {}", g[x]));
            }

            const bool positive = g[x] == GT_MOTION;
            const bool detected = p[x] != 0;

            if (detected && positive)       ++tp;
            else if (detected)              ++fp;
            else if (positive)              ++fn;
            else                            ++tn;
        }
    }
}

double Confusion::recall() const    { return ratio(tp, tp + fn); }
double Confusion::precision() const { return ratio(tp, tp + fp); }
double Confusion::fpr() const       { return ratio(fp, fp + tn); }
double Confusion::fnr() const       { return ratio(fn, tp + fn); }
double Confusion::pwc() const       { return 100.0 * ratio(fn + fp, tp + fn + fp + tn); }

double Confusion::f_measure() const
{
    const double p = precision();
    const double r = recall();
    return (p + r) == 0.0 ? 0.0 : 2.0 * p * r / (p + r);
}

CdnetVideo::CdnetVideo(std::filesystem::path dir)
    : dir_(std::move(dir))
{
    std::ifstream temporal(dir_ / "temporalROI.txt");
    if (!(temporal >> first_ >> last_) || first_ < 1 || last_ < first_)
    {
        throw std::runtime_error("cannot read temporalROI.txt in " + dir_.string());
    }
    roi_ = read_gray(dir_ / "ROI.bmp");
}

cv::Mat CdnetVideo::input(int frame) const
{
    return read_gray(dir_ / "input" / std::format("in{:06}.jpg", frame));
}

cv::Mat CdnetVideo::groundtruth(int frame) const
{
    return read_gray(dir_ / "groundtruth" / std::format("gt{:06}.png", frame));
}
