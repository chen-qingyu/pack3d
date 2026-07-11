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

} // namespace

json run(const json& j) noexcept
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
    for (const auto& bt : problem.box_types)
    {
        box_type_map[bt.id] = bt;
    }
    std::map<std::string, Box> box_map;
    bool has_weight_info = false;
    for (const auto& bx : problem.boxes)
    {
        box_map[bx.id] = bx;
        if (bx.weight.has_value())
        {
            has_weight_info = true;
        }
    }

    // 问题概要
    spdlog::info("Input: {} boxes, {} box types, {} container types",
                 problem.boxes.size(), problem.box_types.size(), problem.container_types.size());
    spdlog::info("Algorithm: {}", algorithm_to_string(problem.algorithm));
    spdlog::info("Constraints: time limit {} s, support rate {:.2f}, platform limit {}, tender limit {}",
                 problem.time_limit, problem.support_rate, problem.platform_limit, problem.tender_limit);

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

} // namespace pack3d
