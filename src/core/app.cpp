#include "app.hpp"

#include <memory>
#include <string>
#include <vector>

#include <spdlog/fmt/ranges.h>
#include <spdlog/fmt/std.h>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "algorithm/bsg/packer.hpp"
#include "algorithm/gep/packer.hpp"
#include "algorithm/glc/packer.hpp"
#include "algorithm/rgs/packer.hpp"
#include "io.hpp"
#include "packer_base.hpp"
#include "palletizer.hpp"
#include "tool.hpp"

namespace pack3d
{

namespace
{

std::unique_ptr<PackerBase> make_packer(
    Algorithm algo,
    const Problem& problem,
    const std::map<std::string, BoxType>& box_type_map,
    const std::map<std::string, Box>& box_map,
    bool has_weight_info)
{
    switch (algo)
    {
        case Algorithm::GEP:
            return std::make_unique<GepPacker>(problem, box_type_map, box_map, has_weight_info);
        case Algorithm::GLC:
            return std::make_unique<GlcPacker>(problem, box_type_map, box_map, has_weight_info);
        case Algorithm::RGS:
            return std::make_unique<RgsPacker>(problem, box_type_map, box_map, has_weight_info);
        case Algorithm::BSG:
            return std::make_unique<BsgPacker>(problem, box_type_map, box_map, has_weight_info);
        default:
            return nullptr;
    }
}

// 构建 box_type/box 索引表，并探测箱子是否含重量信息
void build_index_maps(const Problem& p,
                      std::map<std::string, BoxType>& bt_map,
                      std::map<std::string, Box>& box_map,
                      bool& has_weight_info)
{
    for (const auto& bt : p.box_types)
    {
        bt_map[bt.id] = bt;
    }
    has_weight_info = false;
    for (const auto& bx : p.boxes)
    {
        box_map[bx.id] = bx;
        has_weight_info |= bx.weight.has_value();
    }
}

json run_impl(const json& j)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    auto schema_errors = validate_schema(j);
    if (!schema_errors.empty())
    {
        spdlog::error("Input schema validation failed");
        Solution s;
        s.status = SolveStatus::Invalid;
        s.violations = std::move(schema_errors);
        return json(s);
    }

    auto problem = j.get<Problem>();

    auto violations = pre_validate_input(problem);
    if (!violations.empty())
    {
        spdlog::error("Input validation failed");
        Solution s;
        s.status = SolveStatus::Invalid;
        s.violations = std::move(violations);
        return json(s);
    }

    // 构建索引表
    std::map<std::string, BoxType> box_type_map;
    std::map<std::string, Box> box_map;
    bool has_weight_info = false;
    build_index_maps(problem, box_type_map, box_map, has_weight_info);

    // 问题概要
    spdlog::info("Input: {} boxes, {} box types, {} container types",
                 problem.boxes.size(), problem.box_types.size(), problem.container_types.size());
    spdlog::info("Algorithm: {}", algorithm_to_string(problem.algorithm));
    spdlog::info("Constraints: time limit {} s, support rate {:.2f}, platform limit {}, tender limit {}",
                 problem.time_limit, problem.support_rate, problem.platform_limit, problem.tender_limit);

    if (problem.pallet_types.empty())
    {
        // 现有路径不变（无 pallet_types 时零新增求解调用，保证既有测试确定性）
        spdlog::info("===Algorithm Start===");
        auto packer = make_packer(problem.algorithm, problem, box_type_map, box_map, has_weight_info);
        Solution solution = packer->pack();
        spdlog::info("===Algorithm End===");

        if (solution.status != SolveStatus::Complete)
        {
            spdlog::warn("Result status: {} ({} packed / {} unpacked)",
                         status_to_string(solution.status), solution.packed_box_count, solution.unpacked_box_count);
        }
        spdlog::info("Time used: {:.3f} s", solution.elapsed_second);

        return json(solution);
    }

    // ===== 装托模式：两级流水线（装托 → 改写 → 装车）=====
    spdlog::info("===Palletizing Start===");

    // 装托 problem：support_rate = 装托专用支撑率；装托阶段不应用车厢卸货顺序/平台数约束
    Problem pallet_problem = problem;
    pallet_problem.support_rate = problem.pallet_support_rate;
    pallet_problem.route.reset();
    pallet_problem.platform_limit.reset();

    auto packer1 = make_packer(problem.algorithm, pallet_problem, box_type_map, box_map, has_weight_info);
    const double pallet_budget = std::min(problem.time_limit * 0.2, 15.0);
    TimeChecker::init(pallet_budget);

    std::vector<Box> unpalletized;
    std::vector<PalletLoad> pallet_loads = palletize(pallet_problem, *packer1, unpalletized);
    spdlog::info("Palletizing: {} pallets, {} box(es) not palletized",
                 pallet_loads.size(), unpalletized.size());
    spdlog::info("===Palletizing End===");

    // 改写 + 剩余时间预算（装托用时计入总耗时）
    const double pallet_elapsed = TimeChecker::elapsed();
    const double remaining_time = std::max(problem.time_limit - pallet_elapsed, 0.0);
    Problem new_problem = transform_pallet(problem, pallet_loads, unpalletized, problem.pallet_fallback);
    new_problem.time_limit = remaining_time;

    std::map<std::string, BoxType> np_bt_map;
    std::map<std::string, Box> np_box_map;
    bool np_has_weight = false;
    build_index_maps(new_problem, np_bt_map, np_box_map, np_has_weight);

    spdlog::info("===Algorithm Start===");
    auto packer2 = make_packer(problem.algorithm, new_problem, np_bt_map, np_box_map, np_has_weight);
    Solution solution = packer2->pack();
    spdlog::info("===Algorithm End===");

    // 总用时 = 装托 + 装车
    solution.elapsed_second += pallet_elapsed;

    expand_pallet_solution(solution, pallet_loads, unpalletized, problem.pallet_fallback);

    if (solution.status != SolveStatus::Complete)
    {
        spdlog::warn("Result status: {} ({} packed / {} unpacked)",
                     status_to_string(solution.status), solution.packed_box_count, solution.unpacked_box_count);
    }
    spdlog::info("Time used: {:.3f} s", solution.elapsed_second);

    return json(solution);
}

} // namespace

json run(const json& j)
{
    try
    {
        return run_impl(j);
    }
    catch (const std::exception& e)
    {
        // 任何未预见的异常都返回 invalid，保证任意输入都有输出（不拖垮宿主进程）
        spdlog::error("Unexpected exception: {}", e.what());
        Solution s;
        s.status = SolveStatus::Invalid;
        s.violations.push_back(std::string("internal error: ") + e.what());
        return json(s);
    }
}

} // namespace pack3d
