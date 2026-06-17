#include "packer.hpp"

#include <algorithm>
#include <cassert>
#include <numeric>
#include <set>

#include <spdlog/spdlog.h>

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

    // --- 收尾 ---
    auto now = std::chrono::steady_clock::now();
    bool timed_out = std::chrono::duration<double>(now - state.start_time).count() >= state.time_limit;

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
        if (!check_time(state))
        {
            return false;
        }

        if (!placer_.place_next_box(state))
        {
            // 无法在任何已打开容器中放置剩余箱子
            if (check_tender_limit(state))
            {
                return false;
            }

            if (!open_new_container(state))
            {
                return false;
            }
        }
    }

    // 所有箱子已放置
    update_best(state);
    return true;
}

// 智能选择容器类型
bool Packer::open_new_container(SearchState& state)
{
    // 收集可用的容器类型
    std::vector<const ContainerType*> available;
    for (const auto& ct : problem_.container_types)
    {
        auto usage_it = state.container_type_usage.find(ct.id);
        int used = (usage_it != state.container_type_usage.end()) ? usage_it->second : 0;
        if (!ct.quantity_limit.has_value() || used < ct.quantity_limit.value())
        {
            available.push_back(&ct);
        }
    }
    if (available.empty())
    {
        return false;
    }

    // 计算剩余总体积
    int64_t remaining_volume = 0;
    for (const auto& bx : state.remaining_boxes)
    {
        remaining_volume += box_type_map_.at(bx.box_type_id).size.volume();
    }

    // 按体积升序排列
    std::sort(available.begin(), available.end(),
              [](const ContainerType* a, const ContainerType* b)
              {
                  return a->inner_size.volume() < b->inner_size.volume();
              });

    // 选能装下全部剩余体积的最小容器
    const ContainerType* fallback = available.back();
    const ContainerType* best = nullptr;
    for (auto* ct : available)
    {
        if (ct->inner_size.volume() < remaining_volume)
        {
            continue;
        }
        fallback = ct;

        // 检查所有剩余箱子能否在某个朝向下放入此容器
        bool all_fit = true;
        for (const auto& bx : state.remaining_boxes)
        {
            auto& bt = box_type_map_.at(bx.box_type_id);
            bool box_fits = false;
            for (auto o : bt.allowed_orientations)
            {
                auto os = orient_size(bt.size, o);
                if (os.dx <= ct->inner_size.x &&
                    os.dy <= ct->inner_size.y &&
                    os.dz <= ct->inner_size.z)
                {
                    box_fits = true;
                    break;
                }
            }
            if (!box_fits)
            {
                all_fit = false;
                break;
            }
        }
        if (all_fit)
        {
            best = ct;
            break;
        }
    }
    if (!best)
    {
        best = fallback;
    }

    // 创建新容器实例
    ContainerLoad load;
    load.instance_id = fmt::format("container_{}", state.next_container_instance++);
    load.type_id = best->id;
    load.type = &state.container_type_map[best->id];
    state.extreme_points[load.instance_id].push_back({0, 0, 0});

    state.open_containers.push_back(std::move(load));
    state.container_type_usage[best->id] =
        (state.container_type_usage[best->id]) + 1;

    return true;
}

bool Packer::check_tender_limit(SearchState& state)
{
    if (!problem_.tender_limit.has_value())
    {
        return false;
    }

    int limit = problem_.tender_limit.value();

    for (const auto& [group, touched_containers] : state.group_spread)
    {
        if (static_cast<int>(touched_containers.size()) < limit)
        {
            continue;
        }

        std::vector<std::string> remaining_of_group;
        for (const auto& bx : state.remaining_boxes)
        {
            if (bx.group == group)
            {
                remaining_of_group.push_back(bx.id);
            }
        }
        if (remaining_of_group.empty())
        {
            continue;
        }

        for (const auto& box_id : remaining_of_group)
        {
            auto bit = std::find_if(state.remaining_boxes.begin(),
                                    state.remaining_boxes.end(),
                                    [&](const Box& b)
                                    { return b.id == box_id; });
            if (bit == state.remaining_boxes.end())
            {
                continue;
            }

            const auto& box = *bit;
            auto& bt = box_type_map_.at(box.box_type_id);

            bool can_place = false;
            for (const auto& cid : touched_containers)
            {
                auto cit = std::find_if(state.open_containers.begin(),
                                        state.open_containers.end(),
                                        [&](const ContainerLoad& c)
                                        { return c.instance_id == cid; });
                if (cit == state.open_containers.end())
                {
                    continue;
                }

                const auto& container = *cit;
                auto ep_it = state.extreme_points.find(container.instance_id);
                if (ep_it == state.extreme_points.end())
                {
                    continue;
                }
                for (const auto& ep : ep_it->second)
                {
                    for (auto orient : bt.allowed_orientations)
                    {
                        auto osize = orient_size(bt.size, orient);
                        if (!check_boundary(*container.type, ep, osize))
                        {
                            continue;
                        }
                        if (check_overlap(ep, osize, container.placements, box_type_map_))
                        {
                            continue;
                        }
                        can_place = true;
                        break;
                    }
                    if (can_place)
                        break;
                }
                if (can_place)
                    break;
            }

            if (!can_place)
            {
                state.infeasible = true;
                return true;
            }
        }
    }

    return false;
}

bool Packer::check_time(const SearchState& state) const
{
    auto elapsed = std::chrono::steady_clock::now() - state.start_time;
    auto elapsed_sec = std::chrono::duration<double>(elapsed).count();
    return elapsed_sec < state.time_limit;
}

} // namespace hypercube::sgep
