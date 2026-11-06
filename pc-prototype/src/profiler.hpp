#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

// Accumulated wall time of one pipeline stage.
struct StageTime
{
    std::string name;
    double total_us = 0.0;
    std::int64_t calls = 0;

    double mean_us() const { return calls == 0 ? 0.0 : total_us / static_cast<double>(calls); }
};

// Times the enclosing scope and adds the result to a StageTime when the scope
// ends (RAII). A null stage makes it a no-op, so profiling can be switched off.
class ScopedTimer
{
    using Clock = std::chrono::steady_clock;

public:
    explicit ScopedTimer(StageTime* stage)
        : stage_(stage), start_(Clock::now())
    {
    }

    ~ScopedTimer()
    {
        if (stage_ != nullptr)
        {
            const std::chrono::duration<double, std::micro> elapsed = Clock::now() - start_;
            stage_->total_us += elapsed.count();
            ++stage_->calls;
        }
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    StageTime* stage_;
    Clock::time_point start_;
};

class Profiler
{
public:
    // Returns the named stage, creating it on first use. The pointer stays
    // valid for the profiler's lifetime: std::deque never moves existing
    // elements when appending, unlike std::vector.
    StageTime* stage(std::string_view name)
    {
        for (StageTime& s : stages_)
        {
            if (s.name == name) return &s;
        }
        stages_.push_back(StageTime{std::string(name)});
        return &stages_.back();
    }

    double mean_us(std::string_view name) const
    {
        for (const StageTime& s : stages_)
        {
            if (s.name == name) return s.mean_us();
        }
        return 0.0;
    }

    const std::deque<StageTime>& stages() const { return stages_; }

private:
    std::deque<StageTime> stages_;
};
