#include "solver.hpp"

#include <memory>

#include <spdlog/fmt/ranges.h> // log vector
#include <spdlog/fmt/std.h>    // log optional
#include <spdlog/spdlog.h>

#include "algorithm/bsg/packer.hpp"
#include "algorithm/gep/packer.hpp"
#include "algorithm/glc/packer.hpp"
#include "algorithm/rgs/packer.hpp"
#include "io.hpp"
#include "objectives.hpp"
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

SolverEngine::SolverEngine(const Problem& problem)
    : problem_(problem)
{
    for (const auto& bt : problem.box_types)
    {
        box_type_map_[bt.id] = bt;
    }
    for (const auto& bx : problem.boxes)
    {
        box_map_[bx.id] = bx;
        if (bx.weight.has_value())
        {
            has_weight_info_ = true;
        }
    }
}

// 主入口
Solution SolverEngine::solve()
{
    // 问题概要
    spdlog::info("Input: {} boxes, {} box types, {} container types",
                 problem_.boxes.size(), problem_.box_types.size(), problem_.container_types.size());
    spdlog::info("Algorithm: {}", algorithm_to_string(problem_.algorithm));
    spdlog::info("Objectives: {}", default_objective_keys());
    spdlog::info("Constraints: time limit {} s, support rate {:.2f}, platform limit {}, tender limit {}",
                 problem_.time_limit, problem_.support_rate, problem_.platform_limit, problem_.tender_limit);

    // 运行算法
    spdlog::info("===Algorithm Start===");
    auto packer = make_packer(problem_.algorithm, problem_, box_type_map_,
                              box_map_, has_weight_info_);
    Solution solution = packer->pack();
    spdlog::info("===Algorithm End===");

    // 输出状态
    if (solution.status != SolveStatus::Complete)
    {
        spdlog::warn("Result status: {} ({} packed / {} unpacked)",
                     status_to_string(solution.status), solution.packed_box_count, solution.unpacked_box_count);
    }
    spdlog::info("Time used: {:.3f} s", solution.elapsed_second);

    return solution;
}

} // namespace pack3d
