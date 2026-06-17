#pragma once

#include <chrono>

namespace hypercube
{

class TimeChecker
{
public:
    static void init(double limit_sec) noexcept;
    static bool check();
    static double elapsed() noexcept;

private:
    static std::chrono::steady_clock::time_point start_time_;
    static double time_limit_;
};

} // namespace hypercube
