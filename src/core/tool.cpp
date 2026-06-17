#include "tool.hpp"

namespace hypercube
{

std::chrono::steady_clock::time_point TimeChecker::start_time_{};
double TimeChecker::time_limit_ = 0.0;

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

} // namespace hypercube
