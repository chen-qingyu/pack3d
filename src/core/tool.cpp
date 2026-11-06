#include "tool.hpp"

#include <algorithm>

namespace pack3d
{

std::chrono::steady_clock::time_point TimeChecker::start_time_{};
double TimeChecker::time_limit_ = 0.0;

TimeChecker::ScopedLimit::ScopedLimit(double seconds) noexcept
    : original_(TimeChecker::limit())
{
    TimeChecker::time_limit_ =
        std::min(original_, TimeChecker::elapsed() + std::max(0.0, seconds));
}

TimeChecker::ScopedLimit::~ScopedLimit()
{
    TimeChecker::time_limit_ = original_;
}

void TimeChecker::init(double limit_sec) noexcept
{
    start_time_ = std::chrono::steady_clock::now();
    time_limit_ = limit_sec;
}

bool TimeChecker::check()
{
    return elapsed() < time_limit_;
}

double TimeChecker::elapsed() noexcept
{
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - start_time_).count();
}

double TimeChecker::limit() noexcept
{
    return time_limit_;
}

} // namespace pack3d
