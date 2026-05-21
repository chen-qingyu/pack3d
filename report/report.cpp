#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/app.hpp"

namespace fs = std::filesystem;

// 全局拦截 new/delete 以追踪内存峰值
static std::atomic<size_t> g_allocated{0};
static std::atomic<size_t> g_peak{0};

static void track_alloc(size_t size)
{
    size_t cur = g_allocated.fetch_add(size) + size;
    size_t peak = g_peak.load();
    while (cur > peak && !g_peak.compare_exchange_weak(peak, cur))
    {
    }
}

static void track_free(size_t size)
{
    g_allocated.fetch_sub(size);
}

void* operator new(size_t size)
{
    void* p = std::malloc(size);
    track_alloc(size);
    return p;
}

void* operator new[](size_t size)
{
    void* p = std::malloc(size);
    track_alloc(size);
    return p;
}

void operator delete(void* p, size_t size) noexcept
{
    track_free(size);
    std::free(p);
}

void operator delete[](void* p, size_t size) noexcept
{
    track_free(size);
    std::free(p);
}

void operator delete(void* p) noexcept
{
    // size unknown via free; peak will be slightly inflated, acceptable
    std::free(p);
}

void operator delete[](void* p) noexcept
{
    std::free(p);
}

static size_t get_peak_bytes()
{
    return g_peak.load();
}

// 测试结果
struct FileResult
{
    std::string filename;
    double volume_rate; // %
    double duration;    // s
    double memory_kb;   // KB
};

int main()
{
    if (!fs::exists("data/br/"))
    {
        spdlog::warn("data/br/ not found, return.");
        return 1;
    }

    std::vector<FileResult> results;
    for (const auto& entry : fs::directory_iterator("data/br/"))
    {
        auto path = entry.path();

        // 读取输入
        std::ifstream ifs(path);
        std::stringstream buf;
        buf << ifs.rdbuf();
        std::string json_input = buf.str();

        // 重置追踪，记录起始时间
        g_allocated.store(0);
        g_peak.store(0);
        auto t0 = std::chrono::steady_clock::now();

        // 运行求解器
        spdlog::set_level(spdlog::level::off);
        auto j = hypercube::run_solver(json_input, false);
        spdlog::set_level(spdlog::level::info);

        auto t1 = std::chrono::steady_clock::now();
        size_t mem_peak = get_peak_bytes();

        double volume_rate = j["result"]["containers"][0]["load_summary"]["volume_rate"].get<double>() * 100.0;

        double duration = std::chrono::duration<double>(t1 - t0).count();
        double memory_kb = static_cast<double>(mem_peak) / 1024.0;

        std::string fname = path.filename().string();
        results.push_back({fname, volume_rate, duration, memory_kb});

        spdlog::info("{} - rate: {:.2f}%, duration: {:.3f} s, memory: {:.0f} KB",
                     fname, volume_rate, duration, memory_kb);
    }

    // 写 CSV
    std::ofstream csv("report/report.csv");
    csv << "filename,volume_rate(%),duration(s),memory(KB)\n";
    for (const auto& r : results)
    {
        csv << fmt::format("{},{:.2f},{:.3f},{:.0f}\n", r.filename, r.volume_rate, r.duration, r.memory_kb);
    }

    // 写 TXT
    double sum_rate = 0, sum_dur = 0, sum_mem = 0;
    for (const auto& r : results)
    {
        sum_rate += r.volume_rate;
        sum_dur += r.duration;
        sum_mem += r.memory_kb;
    }
    double avg_rate = sum_rate / results.size();
    double avg_dur = sum_dur / results.size();
    double avg_mem = sum_mem / results.size();

    std::ofstream txt("report/report.txt");
    txt << "Packing Algorithm Performance Report\n\n";
    txt << fmt::format("Total Files Tested: {}\n", results.size());
    txt << fmt::format("Average Volume Rate: {:.2f}%\n", avg_rate);
    txt << fmt::format("Average Duration: {:.3f} s\n", avg_dur);
    txt << fmt::format("Average Memory Usage: {:.0f} KB\n", avg_mem);

    return 0;
}
