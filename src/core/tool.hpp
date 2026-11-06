#pragma once

#include <chrono>

namespace pack3d
{

class TimeChecker
{
public:
    // 临时收紧截止时间，离开作用域后恢复全局时限。
    // 用于后处理试探，避免单次失败的重求解耗光整个求解预算。
    class ScopedLimit
    {
    public:
        explicit ScopedLimit(double seconds) noexcept;
        ~ScopedLimit();

        ScopedLimit(const ScopedLimit&) = delete;
        ScopedLimit& operator=(const ScopedLimit&) = delete;

    private:
        double original_;
    };

    static void init(double limit_sec) noexcept;
    static bool check();
    static double elapsed() noexcept;
    static double limit() noexcept;

private:
    static std::chrono::steady_clock::time_point start_time_;
    static double time_limit_;
};

} // namespace pack3d
