#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>

#include "core/io.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    argparse::ArgumentParser program("hypercube", "0.1.0");

    program.add_argument("input")
        .help("The input JSON file path");

    program.add_argument("--debug")
        .help("Enable debug output (including violation information)")
        .default_value(false)
        .implicit_value(true);

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
    bool debug = program.get<bool>("--debug");

    // 读取输入文件
    std::ifstream ifs(input_file);
    if (!ifs.is_open())
    {
        spdlog::error("Cannot open input file: {}", input_file);
        return 1;
    }
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string json_input = buffer.str();

    spdlog::info("Reading input: \"{}\"", input_file);

    // 运行求解器
    auto t0 = std::chrono::steady_clock::now();
    std::string json_output = hypercube::run_solver(json_input, debug);
    auto t1 = std::chrono::steady_clock::now();

    // 构建输出路径: result/<input_stem>.json
    fs::path in_path(input_file);
    fs::path out_dir = fs::path("result");
    fs::path out_file = out_dir / (in_path.stem().string() + ".json");

    std::error_code ec;
    fs::create_directories(out_dir, ec);

    std::ofstream ofs(out_file);
    if (!ofs.is_open())
    {
        spdlog::error("Cannot write output file: {}", out_file.string());
        return 1;
    }
    ofs << json_output;
    ofs << std::endl;

    auto elapsed = std::chrono::duration<double>(t1 - t0).count();
    spdlog::info("Results written to: \"{}\"", out_file.string());
    spdlog::info("Time used: {:.3f} s", elapsed);

    return 0;
}
