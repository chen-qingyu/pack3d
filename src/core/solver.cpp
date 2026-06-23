#include "solver.hpp"

#include <set>

#include <spdlog/fmt/ranges.h> // log vector
#include <spdlog/fmt/std.h>    // log optional
#include <spdlog/spdlog.h>

#include "algorithm/gep/packer.hpp"
#include "algorithm/glc/packer.hpp"
#include "algorithm/rgs/packer.hpp"
#include "io.hpp"
#include "tool.hpp"

namespace hypercube
{

SolverEngine::SolverEngine(const Problem& problem)
    : problem_(problem)
{
    for (const auto& bt : problem.box_types)
    {
        box_type_map_[bt.id] = bt;
    }
    for (const auto& ct : problem.container_types)
    {
        container_type_map_[ct.id] = ct;
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
    // 初始化计时器
    TimeChecker::init(problem_.time_limit);

    // 问题概要
    spdlog::info("Input: {} boxes, {} box types, {} container types",
                 problem_.boxes.size(), problem_.box_types.size(), problem_.container_types.size());
    spdlog::info("Algorithm: {}", algorithm_to_string(problem_.algorithm));
    const auto& keys = problem_.objective_keys.empty() ? default_objective_keys() : problem_.objective_keys;
    spdlog::info("Objectives: {}", keys);
    spdlog::info("Constraints: time limit {} s, support rate {:.2f}, platform limit {}, tender limit {}",
                 problem_.time_limit, problem_.support_rate, problem_.platform_limit, problem_.tender_limit);

    // 运行算法
    spdlog::info("===Algorithm Start===");
    Solution solution;
    switch (problem_.algorithm)
    {
        case Algorithm::GLC:
        {
            glc::Packer glc(problem_, box_type_map_, box_map_, has_weight_info_);
            solution = glc.pack();
            break;
        }
        case Algorithm::RGS:
        {
            rgs::Packer rgs(problem_, box_type_map_, container_type_map_, box_map_, has_weight_info_);
            solution = rgs.pack();
            break;
        }
        case Algorithm::GEP:
        {
            gep::Packer gep(problem_, box_type_map_, container_type_map_, box_map_, has_weight_info_);
            solution = gep.pack();
            break;
        }
    }
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

} // namespace hypercube
