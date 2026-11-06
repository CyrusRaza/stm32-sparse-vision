#include "projection_detector.hpp"

namespace
{

// Safety net; real masks stop splitting after a few levels.
constexpr int MAX_DEPTH = 16;

} // namespace

ProjectionDetector::ProjectionDetector(const ProjectionParams& params)
    : params_(params)
{
}

const std::vector<cv::Rect>& ProjectionDetector::detect(const cv::Mat& mask)
{
    CV_Assert(mask.type() == CV_8UC1);
    boxes_.clear();
    cut(mask, cv::Rect(0, 0, mask.cols, mask.rows), true, false, 0);
    return boxes_;
}

// Splits region along one axis and recurses into each piece along the other
// axis. A region is final once two consecutive projections (one per axis)
// each find a single interval, i.e. it no longer splits either way.
void ProjectionDetector::cut(const cv::Mat& mask, cv::Rect region, bool along_x, bool prev_single, int depth)
{
    project(mask, region, along_x);

    // Local because the recursive calls below overwrite profile_.
    std::vector<Interval> intervals;
    find_intervals(intervals);

    const bool single = intervals.size() == 1;
    for (const Interval& iv : intervals)
    {
        cv::Rect sub = region;
        if (along_x)
        {
            sub.x += iv.begin;
            sub.width = iv.end - iv.begin;
        }
        else
        {
            sub.y += iv.begin;
            sub.height = iv.end - iv.begin;
        }

        if ((single && prev_single) || depth >= MAX_DEPTH)
        {
            if (sub.area() >= params_.min_area)
            {
                boxes_.push_back(sub);
            }
        }
        else
        {
            cut(mask, sub, !along_x, single, depth + 1);
        }
    }
}

// Counts motion pixels per column (along_x) or per row of region into profile_.
void ProjectionDetector::project(const cv::Mat& mask, cv::Rect region, bool along_x)
{
    profile_.assign(along_x ? region.width : region.height, 0);

    for (int y = 0; y < region.height; ++y)
    {
        const uchar* row = mask.ptr<uchar>(region.y + y) + region.x;
        if (along_x)
        {
            for (int x = 0; x < region.width; ++x)
            {
                profile_[x] += row[x] != 0;
            }
        }
        else
        {
            int count = 0;
            for (int x = 0; x < region.width; ++x)
            {
                count += row[x] != 0;
            }
            profile_[y] = count;
        }
    }
}

// Turns profile_ into intervals of active bins, merging runs whose gap is at
// most max_gap bins (the 1-D counterpart of a morphological close).
void ProjectionDetector::find_intervals(std::vector<Interval>& out) const
{
    out.clear();
    int start = -1;
    int last = -1;

    for (int i = 0; i < static_cast<int>(profile_.size()); ++i)
    {
        if (profile_[i] < params_.min_count)
        {
            continue;
        }
        if (start < 0)
        {
            start = i;
        }
        else if (i - last - 1 > params_.max_gap)
        {
            out.push_back({start, last + 1});
            start = i;
        }
        last = i;
    }
    if (start >= 0)
    {
        out.push_back({start, last + 1});
    }
}
