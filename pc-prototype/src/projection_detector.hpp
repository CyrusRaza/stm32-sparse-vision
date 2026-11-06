#pragma once

#include <vector>

#include <opencv2/core.hpp>

struct ProjectionParams
{
    int min_count = 3;  // a row/column is active if it holds at least this many motion pixels
    int max_gap = 4;    // active runs separated by at most this many inactive bins are merged
    int min_area = 10;  // boxes smaller than this many pixels are dropped
};

// ROI extraction by recursive X-Y cut on a binary motion mask.
//
// The mask is projected onto the x axis (motion pixels counted per column) and
// split into intervals of active columns, giving vertical strips. Each strip is
// then projected onto the y axis using only its own pixels, which pairs every
// y interval with the right x interval (no ghost boxes). Axes keep alternating
// until a region no longer splits on either axis; each final region is a box.
class ProjectionDetector
{
public:
    explicit ProjectionDetector(const ProjectionParams& params);

    // Returns the boxes found in mask. The reference stays valid until the
    // next call, which reuses the same storage.
    const std::vector<cv::Rect>& detect(const cv::Mat& mask);

private:
    struct Interval
    {
        int begin;  // first active bin
        int end;    // one past the last active bin
    };

    void cut(const cv::Mat& mask, cv::Rect region, bool along_x, bool prev_single, int depth);
    void project(const cv::Mat& mask, cv::Rect region, bool along_x);
    void find_intervals(std::vector<Interval>& out) const;

    ProjectionParams params_;
    std::vector<int> profile_;
    std::vector<cv::Rect> boxes_;
};
