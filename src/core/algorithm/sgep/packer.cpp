#include "packer.hpp"

#include <cassert>

#include "../../tool.hpp"

namespace hypercube::sgep
{

Packer::Packer(
    const Problem& problem,
    const std::map<std::string, BoxType>& box_type_map,
    const std::map<std::string, ContainerType>& container_type_map,
    const std::map<std::string, Box>& box_map,
    bool has_weight_info)
    : problem_(problem)
    , box_type_map_(box_type_map)
    , container_type_map_(container_type_map)
    , box_map_(box_map)
    , has_weight_info_(has_weight_info)
    , placer_(box_type_map_, problem_, has_weight_info_)
{
}

Solution Packer::pack()
{
    // --- 初始状态 ---
    SearchState state = make_initial_state();

    // --- 运行构造式搜索 ---
    bool all_packed = construct_solution(state);

    bool timed_out = !TimeChecker::check();

    if (state.infeasible)
    {
        return build_solution(state, "blocked");
    }

    if (all_packed)
    {
        if (state.best_feasible.has_value())
        {
            Solution sol = state.best_feasible.value();
            sol.status = "complete";
            return sol;
        }
        return build_solution(state, "complete");
    }

    // 未全部装箱：检查 best_feasible
    if (state.best_feasible.has_value())
    {
        Solution sol = state.best_feasible.value();
        sol.status = "complete";
        return sol;
    }

    // 完全无可行解
    return build_solution(state, timed_out ? "timeout" : "partial");
}

bool Packer::construct_solution(SearchState& state)
{
    while (!state.remaining_boxes.empty())
    {
        if (!TimeChecker::check())
        {
            return false;
        }

        if (!placer_.place_next_box(state))
        {
            return false;
        }
    }

    // 所有箱子已放置
    update_best(state);
    return true;
}

} // namespace hypercube::sgep
