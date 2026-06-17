#include "solver.hpp"

#include <set>

#include <spdlog/spdlog.h>

#include "algorithm/mlhs/packer.hpp"
#include "algorithm/sgep/packer.hpp"

namespace hypercube
{

// 有值则 to_string，无值则 "null"
template <typename T>
inline std::string opt_str(const std::optional<T>& opt) noexcept
{
    return opt.has_value() ? std::to_string(opt.value()) : "null";
}

SolverEngine::SolverEngine(const Problem& problem)
    : problem_(problem)
{
    box_type_map_ = build_box_type_map(problem.box_types);
    container_type_map_ = build_container_type_map(problem.container_types);
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
    log_problem_info();

    spdlog::info("===Algorithm Start===");

    Solution solution;
    if (problem_.algorithm.algorithm == Algorithm::MLHS)
    {
        mlhs::Packer mlhs(problem_, box_type_map_, box_map_, has_weight_info_);
        solution = mlhs.pack();
    }
    else
    {
        sgep::Packer sgep(problem_, box_type_map_, container_type_map_, box_map_, has_weight_info_);
        solution = sgep.pack();
    }

    log_container_stats(solution.container_summaries);

    spdlog::info("===Algorithm End===");

    if (solution.status != "complete")
    {
        spdlog::warn("Result status: {} ({} packed / {} unpacked)",
                     solution.status, solution.packed_box_count, solution.unpacked_box_count);
    }

    spdlog::info("Time used: {:.3f} s", solution.elapsed_second);
    return solution;
}

void SolverEngine::log_problem_info() const
{
    std::set<std::string> unique_box_types;
    for (const auto& bx : problem_.boxes)
    {
        unique_box_types.insert(bx.box_type_id);
    }

    spdlog::info("Input: {} boxes, {} box types, {} container types",
                 problem_.boxes.size(),
                 unique_box_types.size(),
                 problem_.container_types.size());

    if (problem_.algorithm.algorithm == Algorithm::MLHS)
    {
        spdlog::info("Algorithm: MLHS, width: {}", problem_.algorithm.width);
    }
    else
    {
        spdlog::info("Algorithm: SGEP");
    }

    const auto& keys = problem_.objective_keys.empty() ? default_objective_keys() : problem_.objective_keys;
    std::string obj_str;
    for (size_t i = 0; i < keys.size(); ++i)
    {
        if (i > 0)
            obj_str += ", ";
        obj_str += keys[i];
    }
    spdlog::info("Objectives: {}", obj_str);

    spdlog::info("Constraints: time limit {} s, support rate {:.2f}%, platform limit {}, tender limit {}",
                 problem_.time_limit,
                 problem_.support_rate * 100.0,
                 opt_str(problem_.platform_limit),
                 opt_str(problem_.tender_limit));
}

void SolverEngine::log_container_stats(const std::vector<ContainerSummary>& summaries) const
{
    int total_boxes = static_cast<int>(problem_.boxes.size());
    int packed_sofar = 0;
    int idx = 1;
    for (const auto& cs : summaries)
    {
        packed_sofar += cs.packed_count;
        int left = total_boxes - packed_sofar;

        std::string weight_str;
        if (cs.weight_rate.has_value())
        {
            weight_str = fmt::format("{:.2f}%", cs.weight_rate.value() * 100.0);
        }
        else
        {
            weight_str = "null";
        }

        spdlog::info("Container#{} \"{}\": packed {}, left {}, volume rate: {:.2f}%, weight rate: {}",
                     idx, cs.type_id,
                     cs.packed_count, left,
                     cs.volume_rate * 100.0,
                     weight_str);
        ++idx;
    }
}

} // namespace hypercube
