#pragma once

#include <chrono>

namespace pack3d
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

} // namespace pack3d
