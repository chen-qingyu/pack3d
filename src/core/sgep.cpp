#include "sgep.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <numeric>
#include <set>

#include <spdlog/spdlog.h>

namespace hypercube
{

// 默认空平台标识
inline const std::string empty_platform_id;

SgepSolver::SgepSolver(
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
{
}

Solution SgepSolver::solve()
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

SearchState SgepSolver::make_initial_state() const
{
    SearchState s;
    s.box_type_map = box_type_map_;
    s.container_type_map = container_type_map_;
    s.remaining_boxes = problem_.boxes;
    s.start_time = std::chrono::steady_clock::now();
    s.time_limit = problem_.time_limit;
    s.config = &problem_.algorithm;
    s.problem = &problem_;
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

bool SgepSolver::construct_solution(SearchState& state)
{
    while (!state.remaining_boxes.empty())
    {
        if (!check_time(state))
        {
            return false;
        }

        if (!place_next_box(state))
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
bool SgepSolver::open_new_container(SearchState& state)
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
    load.extreme_points.push_back({0, 0, 0});

    state.open_containers.push_back(std::move(load));
    state.container_type_usage[best->id] =
        (state.container_type_usage[best->id]) + 1;

    return true;
}

// 按目标投影选择最优放置
bool SgepSolver::place_next_box(SearchState& state)
{
    struct ScoredPlacement
    {
        const ContainerLoad* container{nullptr};
        const ContainerType* new_container_type{nullptr};
        Position position;
        Orientation orientation{Orientation::XYZ};
        OrientedSize osize;
        bool new_container{false};
    };

    for (size_t bi = 0; bi < state.remaining_boxes.size(); ++bi)
    {
        const auto& box = state.remaining_boxes[bi];
        auto& bt = box_type_map_.at(box.box_type_id);

        ScoredPlacement best;
        ObjectiveVector best_proj;
        bool found = false;

        // 在已有容器中找最优放置
        for (auto& container : state.open_containers)
        {
            std::sort(container.extreme_points.begin(), container.extreme_points.end(),
                      [](const Position& a, const Position& b) noexcept
                      {
                          if (a.z != b.z)
                          {
                              return a.z < b.z;
                          }
                          if (a.y != b.y)
                          {
                              return a.y < b.y;
                          }
                          return a.x < b.x;
                      });

            if (problem_.platform_limit.has_value() && !box.platform.empty())
            {
                auto pr = check_platform_limit_constraint(
                    container, box.platform, problem_.platform_limit.value());
                if (!pr.ok)
                {
                    continue;
                }
            }

            size_t ep_limit = std::min(container.extreme_points.size(), size_t(200));
            for (size_t ei = 0; ei < ep_limit; ++ei)
            {
                const auto& ep = container.extreme_points[ei];
                for (auto orient : bt.allowed_orientations)
                {
                    OrientedSize osize = orient_size(bt.size, orient);

                    if (!check_boundary_constraint(container, ep, osize).ok)
                    {
                        continue;
                    }
                    if (!check_overlap_constraint(container, ep, osize, box_type_map_).ok)
                    {
                        continue;
                    }
                    if (has_weight_info_ && !check_weight_constraint(container, osize, box.weight.value()).ok)
                    {
                        continue;
                    }
                    if (!check_support_constraint(container, ep, osize, problem_.support_rate, box_type_map_).ok)
                    {
                        continue;
                    }

                    if (problem_.route.has_value() && !box.platform.empty())
                    {
                        auto rr = check_route_order_constraint(
                            container, box.platform, ep, osize, problem_.route.value());
                        if (!rr.ok)
                        {
                            continue;
                        }
                    }

                    // 投影目标
                    ObjectiveVector proj = state.current_objective;

                    if (!box.platform.empty() && !container.platforms.count(box.platform))
                    {
                        proj.platform_count += 1;
                    }

                    if (!box.group.empty() && !container.groups.count(box.group))
                    {
                        proj.group_split_sum += 1;
                    }

                    int type_count = 0;
                    for (const auto& c : state.open_containers)
                    {
                        if (c.type)
                            ++type_count;
                    }
                    if (type_count > 0 && container.type)
                    {
                        double old_rate = container.volume_rate();
                        double new_rate = static_cast<double>(container.used_volume + osize.volume()) / static_cast<double>(container.total_volume());
                        double old_sum = proj.avg_volume_rate * type_count;
                        proj.avg_volume_rate = (old_sum - old_rate + new_rate) / type_count;
                    }

                    if (!found || compare_objectives(proj, best_proj, state.objective_keys) < 0)
                    {
                        found = true;
                        best = {&container, nullptr, ep, orient, osize, false};
                        best_proj = proj;
                    }
                }
            }
        }

        // 对已找到的最优容器放置做惰性 fills_container 检测
        if (found && !best.new_container)
        {
            const auto& target = *best.container;
            int64_t cap_left = target.total_volume() - target.used_volume - best.osize.volume();
            bool fills_container = (cap_left <= 0);
            if (!fills_container)
            {
                auto sim_eps = generate_extreme_points(best.position, best.osize, target);
                filter_extreme_points(sim_eps, target, box_type_map_);
                if (sim_eps.empty())
                {
                    fills_container = true;
                }
                else
                {
                    fills_container = true;
                    for (const auto& rb : state.remaining_boxes)
                    {
                        if (rb.id == box.id)
                        {
                            continue;
                        }
                        auto& rbt = box_type_map_.at(rb.box_type_id);
                        for (const auto& sep : sim_eps)
                        {
                            for (auto ro : rbt.allowed_orientations)
                            {
                                auto ros = orient_size(rbt.size, ro);
                                if (check_boundary(*target.type, sep, ros) &&
                                    !check_overlap_any(sep, ros, target.placements, box_type_map_))
                                {
                                    fills_container = false;
                                    break;
                                }
                            }
                            if (!fills_container)
                            {
                                break;
                            }
                        }
                        if (!fills_container)
                        {
                            break;
                        }
                    }
                }
            }
            bool has_remaining = (state.remaining_boxes.size() > 1);
            if (fills_container && has_remaining)
            {
                best_proj.container_count += 1;
                if (!box.platform.empty())
                {
                    best_proj.platform_count += 1;
                }
                if (!box.group.empty())
                {
                    best_proj.group_split_sum += 1;
                }
            }
        }

        // 评估开新容器的选项
        {
            std::vector<const ContainerType*> available;
            for (const auto& ct : problem_.container_types)
            {
                auto it = state.container_type_usage.find(ct.id);
                int used = (it != state.container_type_usage.end()) ? it->second : 0;
                if (!ct.quantity_limit.has_value() || used < ct.quantity_limit.value())
                {
                    available.push_back(&ct);
                }
            }

            for (auto* ct : available)
            {
                Orientation cand_orient = bt.allowed_orientations[0];
                OrientedSize cand_osize = orient_size(bt.size, cand_orient);
                bool fits = false;
                for (auto orient : bt.allowed_orientations)
                {
                    auto os = orient_size(bt.size, orient);
                    if (os.dx <= ct->inner_size.x &&
                        os.dy <= ct->inner_size.y &&
                        os.dz <= ct->inner_size.z)
                    {
                        cand_orient = orient;
                        cand_osize = os;
                        fits = true;
                        break;
                    }
                }
                if (!fits)
                {
                    continue;
                }

                ObjectiveVector proj = state.current_objective;
                proj.container_count += 1;
                if (!box.platform.empty())
                {
                    proj.platform_count += 1;
                }
                if (!box.group.empty())
                {
                    proj.group_split_sum += 1;
                }

                // 预判此容器装完当前箱子后，剩余能力能否装下后续箱子
                {
                    int64_t extra_vol = 0;
                    for (const auto& rb : state.remaining_boxes)
                    {
                        if (rb.id == box.id)
                        {
                            continue;
                        }
                        if (!box.platform.empty() && rb.platform != box.platform)
                        {
                            continue;
                        }
                        extra_vol += box_type_map_.at(rb.box_type_id).size.volume();
                    }
                    int64_t free_cap = ct->inner_size.volume() - cand_osize.volume();
                    if (extra_vol > 0 && extra_vol > free_cap)
                    {
                        int extra = static_cast<int>((extra_vol + ct->inner_size.volume() - 1) / ct->inner_size.volume());
                        proj.container_count += extra;
                        if (!box.platform.empty())
                        {
                            proj.platform_count += extra;
                        }
                        if (!box.group.empty())
                        {
                            proj.group_split_sum += extra;
                        }
                    }
                }

                int type_count = 0;
                for (const auto& c : state.open_containers)
                {
                    if (c.type)
                        ++type_count;
                }
                double new_rate = static_cast<double>(cand_osize.volume()) / static_cast<double>(ct->inner_size.volume());
                double old_sum = proj.avg_volume_rate * type_count;
                proj.avg_volume_rate = (old_sum + new_rate) / (type_count + 1);

                if (!found || compare_objectives(proj, best_proj, state.objective_keys) < 0)
                {
                    found = true;
                    best = {nullptr, ct, {0, 0, 0}, cand_orient, cand_osize, true};
                    best_proj = proj;
                }
            }
        }

        if (!found)
        {
            continue;
        }

        // 执行最优选项
        if (best.new_container)
        {
            auto* ct = best.new_container_type;
            ContainerLoad load;
            load.instance_id = fmt::format("container_{}", state.next_container_instance++);
            load.type_id = ct->id;
            load.type = &state.container_type_map[ct->id];
            load.extreme_points.push_back({0, 0, 0});
            state.open_containers.push_back(std::move(load));
            state.container_type_usage[ct->id] =
                (state.container_type_usage[ct->id]) + 1;

            best.container = &state.open_containers.back();
        }

        Candidate cand;
        cand.box_id = box.id;
        cand.container_instance_id = best.container->instance_id;
        cand.position = best.position;
        cand.orientation = best.orientation;
        cand.osize = best.osize;

        apply_placement(state, cand);

        state.remaining_boxes.erase(state.remaining_boxes.begin() +
                                    static_cast<ptrdiff_t>(bi));
        return true;
    }

    return false;
}

bool SgepSolver::check_tender_limit(SearchState& state)
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
                for (const auto& ep : container.extreme_points)
                {
                    for (auto orient : bt.allowed_orientations)
                    {
                        auto osize = orient_size(bt.size, orient);
                        if (!check_boundary(*container.type, ep, osize))
                        {
                            continue;
                        }
                        if (check_overlap_any(ep, osize, container.placements, box_type_map_))
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

Position SgepSolver::compactify_placement(const ContainerLoad& container,
                                          const Box& box,
                                          Position pos,
                                          const OrientedSize& osize) const
{
    using MakePos = std::function<Position(int32_t)>;
    auto slide = [&](int32_t start, int32_t limit, MakePos make_pos) -> int32_t
    {
        if (start <= limit)
        {
            return start;
        }
        int32_t cur = start;
        int32_t step = 1;
        while (cur - step > limit)
        {
            auto tp = make_pos(cur - step);
            if (check_overlap_any(tp, osize, container.placements, box_type_map_))
            {
                break;
            }
            cur -= step;
            step = std::min(step * 2, cur - limit);
        }
        for (int32_t t = cur - 1; t >= limit; --t)
        {
            auto tp = make_pos(t);
            if (check_overlap_any(tp, osize, container.placements, box_type_map_))
            {
                break;
            }
            cur = t;
        }
        return cur;
    };

    pos.z = slide(pos.z, 0, [&](int32_t v)
                  { return Position{pos.x, pos.y, v}; });
    pos.y = slide(pos.y, 0, [&](int32_t v)
                  { return Position{pos.x, v, pos.z}; });
    pos.x = slide(pos.x, 0, [&](int32_t v)
                  { return Position{v, pos.y, pos.z}; });

    return pos;
}

void SgepSolver::apply_placement(SearchState& state, Candidate& cand)
{
    auto cit = std::find_if(state.open_containers.begin(),
                            state.open_containers.end(),
                            [&](const ContainerLoad& c)
                            { return c.instance_id == cand.container_instance_id; });
    if (cit == state.open_containers.end())
    {
        return;
    }

    auto& container = *cit;

    auto bit = std::find_if(state.remaining_boxes.begin(),
                            state.remaining_boxes.end(),
                            [&](const Box& b)
                            { return b.id == cand.box_id; });
    if (bit == state.remaining_boxes.end())
    {
        return;
    }

    const Box& box = *bit;

    cand.position = compactify_placement(container, box,
                                         cand.position, cand.osize);

    Placement pl;
    pl.box_id = cand.box_id;
    pl.box_type_id = box.box_type_id;
    pl.container_id = cand.container_instance_id;
    pl.position = cand.position;
    pl.orientation = cand.orientation;

    container.placements.push_back(pl);
    container.used_volume += cand.osize.volume();
    if (has_weight_info_)
    {
        container.total_weight += box.weight.value();
    }

    if (!box.platform.empty())
    {
        container.platforms.insert(box.platform);

        int32_t box_min_x = cand.position.x;
        int32_t box_max_x = cand.position.x + cand.osize.dx;

        auto xmax_it = container.platform_x_max.find(box.platform);
        if (xmax_it == container.platform_x_max.end() ||
            box_max_x > xmax_it->second)
        {
            container.platform_x_max[box.platform] = box_max_x;
        }

        auto xmin_it = container.platform_x_min.find(box.platform);
        if (xmin_it == container.platform_x_min.end() ||
            box_min_x < xmin_it->second)
        {
            container.platform_x_min[box.platform] = box_min_x;
        }
    }

    if (!box.group.empty())
    {
        container.groups.insert(box.group);
        state.group_spread[box.group].insert(container.instance_id);
    }

    auto new_eps = generate_extreme_points(cand.position, cand.osize, container);
    container.extreme_points.insert(
        container.extreme_points.end(),
        new_eps.begin(), new_eps.end());

    filter_extreme_points(container.extreme_points, container, box_type_map_);

    state.current_objective = compute_objective(state.open_containers);
}

bool SgepSolver::check_time(const SearchState& state) const
{
    auto elapsed = std::chrono::steady_clock::now() - state.start_time;
    auto elapsed_sec = std::chrono::duration<double>(elapsed).count();
    return elapsed_sec < state.time_limit;
}

void SgepSolver::update_best(SearchState& state)
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

Solution SgepSolver::build_solution(const SearchState& state,
                                    const std::string& status) const
{
    Solution sol;
    sol.status = status;

    auto elapsed = std::chrono::steady_clock::now() - state.start_time;
    sol.elapsed_second = std::chrono::duration<double>(elapsed).count();

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

} // namespace hypercube
