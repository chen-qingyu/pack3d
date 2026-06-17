#include <algorithm>

#include <spdlog/spdlog.h>

#include "../../tool.hpp"
#include "packer.hpp"

namespace hypercube::sgep
{

// 默认空平台标识
inline const std::string empty_platform_id;

SearchState Packer::make_initial_state() const
{
    SearchState s;
    s.container_type_map = container_type_map_;
    s.remaining_boxes = problem_.boxes;
    s.objective_keys = problem_.objective_keys.empty() ? default_objective_keys() : problem_.objective_keys;

    // 按平台分组（空平台视为默认平台），同平台内按体积降序
    std::stable_sort(s.remaining_boxes.begin(), s.remaining_boxes.end(),
                     [&](const Box& a, const Box& b)
                     {
                         const auto& pa = a.platform.empty() ? empty_platform_id : a.platform;
                         const auto& pb = b.platform.empty() ? empty_platform_id : b.platform;
                         if (pa != pb)
                         {
                             return pa < pb;
                         }
                         // 同平台内按体积降序
                         auto& at = box_type_map_.at(a.box_type_id);
                         auto& bt = box_type_map_.at(b.box_type_id);
                         return at.size.volume() > bt.size.volume();
                     });

    return s;
}

void Packer::update_best(SearchState& state)
{
    if (!state.remaining_boxes.empty())
    {
        return;
    }

    auto ov = compute_objective(state.open_containers);

    if (!state.best_feasible.has_value())
    {
        state.best_feasible = build_solution(state, "complete");
        state.best_feasible->objective = ov;
    }
    else
    {
        if (ov.is_better_than(state.best_feasible->objective, state.objective_keys))
        {
            state.best_feasible = build_solution(state, "complete");
            state.best_feasible->objective = ov;
        }
    }
}

Solution Packer::build_solution(const SearchState& state,
                                const std::string& status) const
{
    Solution sol;
    sol.status = status;

    sol.elapsed_second = TimeChecker::elapsed();

    sol.packed_box_count = static_cast<int>(
        problem_.boxes.size() - state.remaining_boxes.size());
    sol.unpacked_box_count = static_cast<int>(state.remaining_boxes.size());

    sol.objective = compute_objective(state.open_containers);

    for (const auto& load : state.open_containers)
    {
        if (load.placements.empty())
        {
            continue;
        }
        ContainerSummary cs;
        cs.type_id = load.type_id;
        if (load.type)
        {
            cs.inner_size = load.type->inner_size;
        }
        cs.packed_count = static_cast<int>(load.placements.size());
        cs.used_volume = load.used_volume;
        cs.volume_rate = load.volume_rate();
        cs.used_weight = has_weight_info_ ? std::optional<double>(load.total_weight) : std::nullopt;
        cs.weight_rate = has_weight_info_ && load.type && load.type->max_weight.has_value()
                             ? std::optional<double>(load.total_weight / load.type->max_weight.value())
                             : std::nullopt;
        cs.max_weight = load.type && load.type->max_weight.has_value()
                            ? std::optional<double>(load.type->max_weight.value())
                            : std::nullopt;
        cs.platforms.assign(load.platforms.begin(), load.platforms.end());
        cs.groups.assign(load.groups.begin(), load.groups.end());

        sol.container_summaries.push_back(cs);
        sol.container_placements.push_back(load.placements);
    }

    for (const auto& bx : state.remaining_boxes)
    {
        sol.unpacked_boxes.push_back(bx.id);
    }

    sol.box_types = problem_.box_types;
    sol.objective_keys = state.objective_keys;

    return sol;
}

} // namespace hypercube::sgep
