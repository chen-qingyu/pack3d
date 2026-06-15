#include <filesystem>
#include <fstream>
#include <string>

#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>

#include "core/app.hpp"

namespace fs = std::filesystem;

// 尝试用 CLI 参数覆盖 JSON 默认值，返回 true 表示可继续
template <typename T>
static bool try_set(json& j, const argparse::ArgumentParser& p, const std::string& arg, const std::string& path)
{
    auto opt = p.present<T>(arg);
    if (!opt)
    {
        return true; // CLI 参数未提供，跳过覆盖
    }

    json cli_val = json(*opt);
    json::json_pointer ptr(path);

    if (j.contains(ptr))
    {
        json& existing = j[ptr];
        if (existing == cli_val)
        {
            return true; // 值相同，无冲突
        }
        spdlog::error("Conflict: {} ({}) conflicts with {} ({})", arg, cli_val.dump(), path, existing.dump());
        return false; // 冲突，无法覆盖
    }

    j[ptr] = cli_val; // 自动创建中间节点
    return true;      // 成功覆盖
}

int main(int argc, char** argv)
{
    argparse::ArgumentParser program("hypercube", "0.1.0");

    program.add_argument("input").help("Input JSON file path");
    program.add_argument("-o", "--output-dir").help("Output directory (default: result/)").default_value("result");
    program.add_argument("-t", "--time-limit").scan<'g', double>().help("Set time limit in seconds");
    program.add_argument("-s", "--support-rate").scan<'g', double>().help("Set support rate (0~1)");
    program.add_argument("--platform-limit").scan<'i', int>().help("Set platform limit");
    program.add_argument("--tender-limit").scan<'i', int>().help("Set tender limit");
    program.add_argument("-a", "--algorithm").help("Set algorithm");
    program.add_argument("--width").scan<'i', int>().help("Set MLHS beam width");

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::exception& e)
    {
        spdlog::error("{}", e.what());
        return 1;
    }

    std::string input_file = program.get<std::string>("input");
    std::string output_dir = program.get<std::string>("-o");

    std::ifstream ifs(input_file);
    if (!ifs.is_open())
    {
        spdlog::error("Cannot open: {}", input_file);
        return 1;
    }

    spdlog::info("Reading input: \"{}\"", input_file);
    json j;
    try
    {
        j = json::parse(ifs);
    }
    catch (const std::exception& e)
    {
        spdlog::error("Failed to parse JSON: {}", e.what());
        return 1;
    }

    // CLI 覆盖
    if (!try_set<double>(j, program, "-t", "/constraints/time_limit") ||
        !try_set<double>(j, program, "-s", "/constraints/support_rate") ||
        !try_set<int>(j, program, "--platform-limit", "/constraints/platform_limit") ||
        !try_set<int>(j, program, "--tender-limit", "/constraints/tender_limit") ||
        !try_set<std::string>(j, program, "-a", "/algorithm/use") ||
        !try_set<int>(j, program, "--width", "/algorithm/config/mlhs/width"))
    {
        return 1;
    }

    // 运行求解器
    auto result = hypercube::run(j);

    // 构建输出路径
    fs::path in_path(input_file);
    fs::path out_dir_path(output_dir);
    fs::path out_file = out_dir_path / (in_path.stem().string() + ".json");

    std::error_code ec;
    fs::create_directories(out_dir_path, ec);

    std::ofstream ofs(out_file);
    if (!ofs.is_open())
    {
        spdlog::error("Cannot write output file: {}", out_file.string());
        return 1;
    }
    ofs << result.dump(2);
    ofs << std::endl;

    spdlog::info("Results written to: \"{}\"", out_file.string());

    return 0;
}
