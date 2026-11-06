// Evaluates motion masks and ROI extraction methods against CDnet 2014 ground
// truth, and profiles the processing time of every stage.
//
// Mask stages: threshold, threshold + open, threshold + open + close.
// ROI methods (run on every mask stage): contour boxes, projection X-Y cut.

#include "boxes.hpp"
#include "cdnet.hpp"
#include "contour_detector.hpp"
#include "motion_detector.hpp"
#include "profiler.hpp"
#include "projection_detector.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <opencv2/core/utility.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

namespace
{

struct Options
{
    MotionParams motion;
    ProjectionParams projection;
    int min_area = 10;
    bool show = false;
    bool per_video = false;
    std::vector<std::string> videos;
};

enum Stage : std::size_t
{
    THRESHOLDED,
    OPENED,
    CLOSED,
    STAGE_COUNT
};

constexpr std::array<const char*, STAGE_COUNT> STAGE_NAMES = {
    "threshold", "threshold+open", "threshold+open+close"};

enum Method : std::size_t
{
    CONTOURS,
    PROJECTIONS,
    METHOD_COUNT
};

constexpr std::array<const char*, METHOD_COUNT> METHOD_NAMES = {"contours", "projections"};

// Scores for one output (a mask or a set of boxes) on one video.
struct Score
{
    Confusion counts;
    std::int64_t frames = 0;            // scored frames
    std::int64_t box_count = 0;         // only used for box methods
    std::int64_t flagged_pixels = 0;    // pixels marked as motion
    std::int64_t total_pixels = 0;

    void add(const cv::Mat& pred, const cv::Mat& gt, const cv::Mat& roi)
    {
        counts.add(pred, gt, roi);
        ++frames;
        flagged_pixels += cv::countNonZero(pred);
        total_pixels += static_cast<std::int64_t>(pred.total());
    }

    double boxes_per_frame() const
    {
        return frames == 0 ? 0.0 : static_cast<double>(box_count) / static_cast<double>(frames);
    }
    double coverage() const
    {
        return total_pixels == 0 ? 0.0 : static_cast<double>(flagged_pixels) / static_cast<double>(total_pixels);
    }
};

struct VideoResult
{
    std::string name;
    std::array<Score, STAGE_COUNT> mask;
    std::array<std::array<Score, STAGE_COUNT>, METHOD_COUNT> boxes;  // [method][stage]
};

std::string roi_stage_name(std::size_t method, std::size_t stage)
{
    return std::format("{} on [{}]", METHOD_NAMES[method], STAGE_NAMES[stage]);
}

void print_usage(const char* prog)
{
    std::cerr << "Usage: " << prog << " [options] <cdnet_video_dir>...\n"
              << "  --thresh N      difference threshold (default 10)\n"
              << "  --open K        opening kernel side (default 3)\n"
              << "  --close K       closing kernel side (default 5)\n"
              << "  --min-area A    minimum box area in px, both ROI methods (default 10)\n"
              << "  --proj-min N    projection: min motion pixels for an active row/column (default 3)\n"
              << "  --proj-gap G    projection: merge runs separated by <= G empty bins (default 4)\n"
              << "  --per-video     print per-video tables, not only the summary\n"
              << "  --show          live preview ('q' closes it)\n"
              << "Example: " << prog << " data/baseline/highway data/baseline/office\n";
}

bool parse_args(int argc, char** argv, Options& opts)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];

        // Reads the value after a flag into out; fails if the flag is last.
        auto next_int = [&](int& out)
        {
            if (i + 1 >= argc) return false;
            out = std::atoi(argv[++i]);
            return true;
        };

        if (arg == "--thresh")          { if (!next_int(opts.motion.threshold)) return false; }
        else if (arg == "--open")       { if (!next_int(opts.motion.open_kernel)) return false; }
        else if (arg == "--close")      { if (!next_int(opts.motion.close_kernel)) return false; }
        else if (arg == "--min-area")   { if (!next_int(opts.min_area)) return false; }
        else if (arg == "--proj-min")   { if (!next_int(opts.projection.min_count)) return false; }
        else if (arg == "--proj-gap")   { if (!next_int(opts.projection.max_gap)) return false; }
        else if (arg == "--per-video")  { opts.per_video = true; }
        else if (arg == "--show")       { opts.show = true; }
        else if (arg.starts_with("--")) { return false; }
        else                            { opts.videos.emplace_back(arg); }
    }
    opts.projection.min_area = opts.min_area;
    return !opts.videos.empty();
}

// Runs one ROI method on a mask (timed), then scores its filled boxes.
// A template rather than a virtual base class: both detectors only need a
// matching detect() signature, resolved at compile time.
template <typename Detector>
void score_rois(Detector& detector, const cv::Mat& mask, const cv::Mat& gt, const cv::Mat& roi,
                StageTime* timer, Score& score, cv::Mat& box_mask)
{
    const std::vector<cv::Rect>* boxes = nullptr;
    {
        ScopedTimer t(timer);
        boxes = &detector.detect(mask);
    }
    render_boxes(*boxes, mask.size(), box_mask);
    score.add(box_mask, gt, roi);
    score.box_count += static_cast<std::int64_t>(boxes->size());
}

// Shows input with boxes from the final mask (green: contours, red:
// projections) | threshold | +open | +close | ground truth.
// Returns false once the user presses 'q'.
bool show_frame(const cv::Mat& gray, const std::array<const cv::Mat*, STAGE_COUNT>& stages, const cv::Mat& gt,
                const std::vector<cv::Rect>& contour_boxes, const std::vector<cv::Rect>& projection_boxes)
{
    std::array<cv::Mat, STAGE_COUNT + 2> tiles;
    cv::cvtColor(gray, tiles[0], cv::COLOR_GRAY2BGR);
    for (const cv::Rect& box : contour_boxes)
    {
        cv::rectangle(tiles[0], box, cv::Scalar(0, 255, 0), 1);
    }
    for (const cv::Rect& box : projection_boxes)
    {
        cv::rectangle(tiles[0], box, cv::Scalar(0, 0, 255), 1);
    }
    for (std::size_t s = 0; s < STAGE_COUNT; ++s)
    {
        cv::cvtColor(*stages[s], tiles[s + 1], cv::COLOR_GRAY2BGR);
    }
    cv::cvtColor(gt, tiles.back(), cv::COLOR_GRAY2BGR);

    cv::Mat view;
    cv::hconcat(tiles.data(), tiles.size(), view);
    cv::imshow("input+boxes (green contours, red projections) | threshold | +open | +close | ground truth", view);
    return cv::waitKey(1) != 'q';
}

// Runs every stage over every frame (so detector state is realistic) but only
// scores frames inside the temporal ROI. Pressing 'q' in the preview closes
// it and finishes the evaluation without display.
VideoResult evaluate(const CdnetVideo& video, const Options& opts, bool& show, Profiler& profiler)
{
    MotionDetector motion(opts.motion);
    motion.set_profiler(&profiler);
    ContourDetector contours(opts.min_area);
    ProjectionDetector projections(opts.projection);

    std::array<std::array<StageTime*, STAGE_COUNT>, METHOD_COUNT> timers{};
    for (std::size_t m = 0; m < METHOD_COUNT; ++m)
    {
        for (std::size_t s = 0; s < STAGE_COUNT; ++s)
        {
            timers[m][s] = profiler.stage(roi_stage_name(m, s));
        }
    }

    VideoResult r;
    r.name = video.name();
    cv::Mat mask;
    cv::Mat box_mask;

    for (int i = 1; i <= video.last_frame(); ++i)
    {
        const cv::Mat gray = video.input(i);
        if (!motion.process(gray, mask) || i < video.first_frame())
        {
            continue;
        }

        const cv::Mat gt = video.groundtruth(i);
        const std::array<const cv::Mat*, STAGE_COUNT> stages = {
            &motion.thresholded(), &motion.opened(), &mask};

        for (std::size_t s = 0; s < STAGE_COUNT; ++s)
        {
            r.mask[s].add(*stages[s], gt, video.roi());
            score_rois(contours, *stages[s], gt, video.roi(), timers[CONTOURS][s], r.boxes[CONTOURS][s], box_mask);
            score_rois(projections, *stages[s], gt, video.roi(), timers[PROJECTIONS][s], r.boxes[PROJECTIONS][s], box_mask);
        }

        if (show && !show_frame(gray, stages, gt, contours.detect(mask), projections.detect(mask)))
        {
            show = false;
            cv::destroyAllWindows();
        }
    }
    return r;
}

// Mean processing time per frame of producing a mask stage.
double mask_cost_us(const Profiler& p, std::size_t stage)
{
    double us = p.mean_us("absdiff") + p.mean_us("threshold") + p.mean_us("store prev frame");
    if (stage >= OPENED) us += p.mean_us("open");
    if (stage >= CLOSED) us += p.mean_us("close");
    return us;
}

double roi_cost_us(const Profiler& p, std::size_t method, std::size_t stage)
{
    return mask_cost_us(p, stage) + p.mean_us(roi_stage_name(method, stage));
}

// Means over videos (CDnet reports a category as the mean of per-video metrics).
struct Means
{
    double recall = 0, precision = 0, f = 0, fpr = 0, fnr = 0, pwc = 0, coverage = 0, boxes = 0;
};

template <typename Select>
Means mean_over_videos(const std::vector<VideoResult>& results, Select select)
{
    Means m;
    for (const VideoResult& r : results)
    {
        const Score& s = select(r);
        m.recall += s.counts.recall();
        m.precision += s.counts.precision();
        m.f += s.counts.f_measure();
        m.fpr += s.counts.fpr();
        m.fnr += s.counts.fnr();
        m.pwc += s.counts.pwc();
        m.coverage += s.coverage();
        m.boxes += s.boxes_per_frame();
    }
    const double n = static_cast<double>(results.size());
    for (double* v : {&m.recall, &m.precision, &m.f, &m.fpr, &m.fnr, &m.pwc, &m.coverage, &m.boxes})
    {
        *v /= n;
    }
    return m;
}

void print_metrics(std::string_view name, double recall, double precision, double f, double fpr,
                   double fnr, double pwc, double coverage)
{
    std::cout << std::format("{:<14}{:>8.3f}{:>11.3f}{:>8.3f}{:>9.4f}{:>8.3f}{:>8.3f}{:>10.3f}",
                             name, recall, precision, f, fpr, fnr, pwc, coverage);
}

template <typename Select>
void print_video_table(std::string_view title, const std::vector<VideoResult>& results, bool boxes, Select select)
{
    std::cout << '\n' << title << '\n'
              << std::format("{:<14}{:>8}{:>11}{:>8}{:>9}{:>8}{:>8}{:>10}{}\n",
                             "video", "recall", "precision", "F", "FPR", "FNR", "PWC", "coverage",
                             boxes ? std::format("{:>13}", "boxes/frame") : "");
    for (const VideoResult& r : results)
    {
        const Score& s = select(r);
        const Confusion& c = s.counts;
        print_metrics(r.name, c.recall(), c.precision(), c.f_measure(), c.fpr(), c.fnr(), c.pwc(), s.coverage());
        std::cout << (boxes ? std::format("{:>13.1f}", s.boxes_per_frame()) : "") << '\n';
    }
    if (results.size() > 1)
    {
        const Means m = mean_over_videos(results, select);
        print_metrics("mean", m.recall, m.precision, m.f, m.fpr, m.fnr, m.pwc, m.coverage);
        std::cout << (boxes ? std::format("{:>13.1f}", m.boxes) : "") << '\n';
    }
}

void print_per_video(const std::vector<VideoResult>& results, const Options& opts)
{
    for (std::size_t s = 0; s < STAGE_COUNT; ++s)
    {
        print_video_table(std::format("Mask after [{}]", STAGE_NAMES[s]), results, false,
                          [s](const VideoResult& r) -> const Score& { return r.mask[s]; });
    }
    for (std::size_t m = 0; m < METHOD_COUNT; ++m)
    {
        for (std::size_t s = 0; s < STAGE_COUNT; ++s)
        {
            print_video_table(std::format("Boxes: {}, filled, min area {} px", roi_stage_name(m, s), opts.min_area),
                              results, true,
                              [m, s](const VideoResult& r) -> const Score& { return r.boxes[m][s]; });
        }
    }
}

void print_summary(const std::vector<VideoResult>& results, const Profiler& profiler)
{
    std::cout << std::format("\nSummary (accuracy: mean over {} videos; cost: mean processing time per frame)\n",
                             results.size())
              << std::format("{:<36}{:>8}{:>11}{:>8}{:>10}{:>13}{:>11}\n",
                             "method", "recall", "precision", "F", "coverage", "boxes/frame", "us/frame");

    for (std::size_t s = 0; s < STAGE_COUNT; ++s)
    {
        const Means m = mean_over_videos(results, [s](const VideoResult& r) -> const Score& { return r.mask[s]; });
        std::cout << std::format("{:<36}{:>8.3f}{:>11.3f}{:>8.3f}{:>10.3f}{:>13}{:>11.1f}\n",
                                 std::format("mask [{}]", STAGE_NAMES[s]), m.recall, m.precision, m.f,
                                 m.coverage, "-", mask_cost_us(profiler, s));
    }
    for (std::size_t method = 0; method < METHOD_COUNT; ++method)
    {
        for (std::size_t s = 0; s < STAGE_COUNT; ++s)
        {
            const Means m = mean_over_videos(
                results, [method, s](const VideoResult& r) -> const Score& { return r.boxes[method][s]; });
            std::cout << std::format("{:<36}{:>8.3f}{:>11.3f}{:>8.3f}{:>10.3f}{:>13.1f}{:>11.1f}\n",
                                     roi_stage_name(method, s), m.recall, m.precision, m.f, m.coverage,
                                     m.boxes, roi_cost_us(profiler, method, s));
        }
    }
}

void print_profile(const Profiler& profiler)
{
    std::cout << "\nProcessing profile (single-threaded, excludes image loading and scoring)\n"
              << std::format("{:<40}{:>10}{:>12}\n", "stage", "calls", "us/call");
    for (const StageTime& s : profiler.stages())
    {
        std::cout << std::format("{:<40}{:>10}{:>12.2f}\n", s.name, s.calls, s.mean_us());
    }
}

} // namespace

int main(int argc, char** argv)
{
    Options opts;
    if (!parse_args(argc, argv, opts))
    {
        print_usage(argv[0]);
        return 1;
    }

    // Single-threaded, so stage timings compare like for like (and resemble a single-core MCU).
    cv::setNumThreads(0);

    Profiler profiler;
    std::vector<VideoResult> results;
    try
    {
        for (const std::string& dir : opts.videos)
        {
            const CdnetVideo video(dir);
            std::cerr << "evaluating " << video.name() << "...\n";
            results.push_back(evaluate(video, opts, opts.show, profiler));
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    const MotionParams& mp = opts.motion;
    const ProjectionParams& pp = opts.projection;
    std::cout << std::format("threshold={} open={}x{} close={}x{} min_area={} proj_min={} proj_gap={}\n",
                             mp.threshold, mp.open_kernel, mp.open_kernel, mp.close_kernel, mp.close_kernel,
                             opts.min_area, pp.min_count, pp.max_gap);
    if (opts.per_video)
    {
        print_per_video(results, opts);
    }
    print_summary(results, profiler);
    print_profile(profiler);
    return 0;
}
