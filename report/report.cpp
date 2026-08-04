#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include <argparse/argparse.hpp>
#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>

#include "core/app.hpp"

namespace fs = std::filesystem;

// 全局拦截 new/delete 以追踪内存峰值
// 前提：
// 1. MSVC STL 容器释放走 sized operator delete(void*, size_t)，正确记账
//    若换 GCC/MinGW，libstdc++ 容器走 unsized delete，g_allocated 只增不减，峰值≈累计分配总量
// 2. 代码库无 alignas/SIMD 等 over-aligned 类型
//
// unsized delete 仅命中唯一多态类型 PackerBase 对象，其全程存活、本就计入峰值，且每次 run 前计数器清零，残留不影响报告峰值
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
    std::string status;
    double volume_rate; // %
    double duration;    // s
    double memory_kb;   // KB
};

// 对 data/br/ 下所有文件跑指定算法，返回结果列表
static std::vector<FileResult> run_algorithm(const std::string& algo, double time_limit)
{
    std::vector<FileResult> results;
    for (const auto& entry : fs::directory_iterator("data/br/"))
    {
        auto path = entry.path();

        std::ifstream ifs(path);
        std::stringstream buf;
        buf << ifs.rdbuf();
        auto input = json::parse(buf.str());
        input["algorithm"] = algo;
        input["constraints"]["time_limit"] = time_limit;

        g_allocated.store(0);
        g_peak.store(0);
        auto t0 = std::chrono::steady_clock::now();

        spdlog::set_level(spdlog::level::off);
        auto j = pack3d::run(input);
        spdlog::set_level(spdlog::level::info);

        auto t1 = std::chrono::steady_clock::now();
        size_t mem_peak = get_peak_bytes();

        std::string status = j["status"].get<std::string>();
        double volume_rate = j["result"]["containers"][0]["volume_rate"].get<double>() * 100.0;

        double duration = std::chrono::duration<double>(t1 - t0).count();
        double memory_kb = static_cast<double>(mem_peak) / 1024.0;

        std::string fname = path.filename().string();
        results.push_back({fname, status, volume_rate, duration, memory_kb});

        spdlog::info("[{}] {} - status: {}, rate: {:.2f}%, duration: {:.3f} s, memory: {:.0f} KB",
                     algo, fname, status, volume_rate, duration, memory_kb);
    }
    return results;
}

static void write_csv(const std::string& algo, const std::vector<FileResult>& results)
{
    std::string csv_path = fmt::format("report/report-{}.csv", algo);
    std::ofstream csv(csv_path);
    csv << "filename,status,volume_rate(%),duration(s),memory(KB)\n";
    for (const auto& r : results)
    {
        csv << fmt::format("{},{},{:.2f},{:.3f},{:.0f}\n", r.filename, r.status, r.volume_rate, r.duration, r.memory_kb);
    }
}

static void write_summary_txt(double time_limit,
                              const std::vector<std::string>& algos,
                              const std::vector<std::vector<FileResult>>& all_results)
{
    std::ofstream txt("report/report.txt");
    txt << "Packing Algorithm Performance Report\n\n";
    txt << fmt::format("Time Limit: {:.0f} s\n", time_limit);
    txt << fmt::format("Total Files: {}\n\n", all_results[0].size());

    txt << fmt::format("{:<8} {:>8} {:>8} {:>8}  {}\n", "Algo", "Rate(%)", "Dur(s)", "Mem(KB)", "Status");
    txt << "----------------------------------------------------------\n";
    for (size_t i = 0; i < algos.size(); ++i)
    {
        const auto& results = all_results[i];
        double sum_rate = 0, sum_dur = 0, sum_mem = 0;
        std::map<std::string, int> status_count;
        for (const auto& r : results)
        {
            sum_rate += r.volume_rate;
            sum_dur += r.duration;
            sum_mem += r.memory_kb;
            status_count[r.status]++;
        }
        auto n = results.size();
        txt << fmt::format("{:<8} {:>8.2f} {:>8.3f} {:>8.0f}  {}\n", algos[i], sum_rate / n, sum_dur / n, sum_mem / n, status_count);
    }
}

int main(int argc, char** argv)
{
    argparse::ArgumentParser program("report", "0.1.0");
    program.add_argument("-a", "--algorithm")
        .help("Set algorithm")
        .choices("gep", "glc", "rgs", "bsg", "all");
    program.add_argument("-t", "--time-limit")
        .help("Set time limit in seconds")
        .scan<'g', double>()
        .default_value(120.0);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::exception& e)
    {
        spdlog::error("{}", e.what());
        return 1;
    }

    std::string algo = program.get<std::string>("-a");
    double time_limit = program.get<double>("-t");

    if (!fs::exists("data/br/"))
    {
        spdlog::warn("data/br/ not found, return.");
        return 1;
    }

    std::vector<std::string> algos = (algo == "all") ? std::vector<std::string>{"gep", "glc", "rgs", "bsg"} : std::vector<std::string>{algo};
    std::vector<std::vector<FileResult>> all_results;
    for (const auto& a : algos)
    {
        auto results = run_algorithm(a, time_limit);
        write_csv(a, results);
        all_results.push_back(std::move(results));
    }
    write_summary_txt(time_limit, algos, all_results);

    return 0;
}
