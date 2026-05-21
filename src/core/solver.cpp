#include "solver.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>
#include <random>
#include <set>

#include <spdlog/spdlog.h>

namespace hypercube
{

// =============================================================
// SolverEngine
// =============================================================
SolverEngine::SolverEngine(const Problem& problem)
    : problem_(problem)
{
    box_type_map_ = build_box_type_map(problem.box_types);
    container_type_map_ = build_container_type_map(problem.container_types);
    for (const auto& bx : problem.boxes)
    {
        box_map_[bx.id] = bx;
    }
}

// =============================================================
// solve — 主入口
// =============================================================
Solution SolverEngine::solve()
{
    // --- 预校验 ---
    auto pre_errors = pre_validate_input(problem_);
    if (!pre_errors.empty())
    {
        Solution s;
        s.status = Status::InvalidInput;
        s.reason = reason::k_invalid_range;
        s.violations = pre_errors;
        return s;
    }

    // --- 初始状态 ---
    SearchState state = make_initial_state();

    spdlog::info("Successfully validated input.");
    spdlog::info("Boxes count: {}, Support rate: {:.0f}%, Platform limit: {}, Tender limit: {}",
                 problem_.boxes.size(),
                 problem_.support_rate * 100.0,
                 problem_.platform_limit.has_value() ? std::to_string(problem_.platform_limit.value()) : "null",
                 problem_.tender_limit.has_value() ? std::to_string(problem_.tender_limit.value()) : "null");

    spdlog::info("===Algorithm Start===");

    // --- 运行构造式搜索 ---
    bool all_packed = construct_solution(state);

    // 记录最终状态的容器统计
    {
        int idx = 1;
        int packed_sofar = 0;
        int total_boxes = static_cast<int>(problem_.boxes.size());
        for (const auto& c : state.open_containers)
        {
            int packed = static_cast<int>(c.placements.size());
            packed_sofar += packed;
            int left = total_boxes - packed_sofar;
            spdlog::info("Container#{} \"{}\": packed {}, left {}, volume rate: {:.2f}%, weight rate: {:.2f}%",
                         idx, c.type_id,
                         packed, left,
                         c.volume_rate() * 100.0,
                         c.total_weight / c.type->max_weight * 100.0);
            ++idx;
        }
    }
    spdlog::info("===Algorithm End===");

    // --- 收尾 ---
    if (state.proven_infeasible)
    {
        return build_solution(state, Status::FailedConstraint,
                              state.failure_reason.c_str());
    }

    if (all_packed)
    {
        if (state.best_feasible.has_value())
        {
            Solution sol = state.best_feasible.value();

            if (!check_time(state))
            {
                sol.status = Status::Success;
                sol.reason = reason::k_feasible;
            }
            else
            {
                sol.status = Status::Success;
                sol.reason = reason::k_optimal;
            }

            // 最终验证
            auto final_errors = final_check_solution(sol, problem_, box_type_map_, box_map_);
            if (!final_errors.empty())
            {
                sol.status = Status::FailedConstraint;
                sol.reason = reason::k_final_check;
                sol.violations = final_errors;
            }
            return sol;
        }
        return build_solution(state, Status::Success, reason::k_feasible);
    }

    // 未全部装箱：检查 best_feasible、超时或多起点
    if (state.best_feasible.has_value())
    {
        Solution sol = state.best_feasible.value();
        sol.status = Status::Success;
        sol.reason = reason::k_feasible;

        auto final_errors = final_check_solution(sol, problem_, box_type_map_, box_map_);
        if (!final_errors.empty())
        {
            sol.status = Status::FailedConstraint;
            sol.reason = reason::k_final_check;
            sol.violations = final_errors;
        }
        return sol;
    }

    // 尚无可行解：若还有容器类型可用则尝试多起点
    if (check_time(state))
    {
        // 检查是否还有未耗尽的容器类型，没有的话多起点也是白费功夫
        bool can_open_any = false;
        for (const auto& ct : problem_.container_types)
        {
            auto it = state.container_type_usage.find(ct.id);
            int used = (it != state.container_type_usage.end()) ? it->second : 0;
            if (!ct.quantity_limit.has_value() || used < ct.quantity_limit.value())
            {
                can_open_any = true;
                break;
            }
        }
        if (can_open_any)
        {
            multi_start_solve(state);
        }
    }

    if (state.best_feasible.has_value())
    {
        Solution sol = state.best_feasible.value();
        sol.status = Status::Success;
        sol.reason = reason::k_feasible;

        auto final_errors = final_check_solution(sol, problem_, box_type_map_, box_map_);
        if (!final_errors.empty())
        {
            sol.status = Status::FailedConstraint;
            sol.reason = reason::k_final_check;
            sol.violations = final_errors;
        }
        return sol;
    }

    // 完全无可行解
    return build_solution(state, Status::Timeout, reason::k_no_solution);
}

// =============================================================
// make_initial_state
// =============================================================
SearchState SolverEngine::make_initial_state(BoxOrder order) const
{
    SearchState s;
    s.box_type_map = box_type_map_;
    s.container_type_map = container_type_map_;
    s.remaining_boxes = problem_.boxes;
    s.start_time = std::chrono::steady_clock::now();
    s.time_limit_seconds = problem_.time_limit_seconds;
    s.config = &problem_.solver_config;
    s.problem = &problem_;

    switch (order)
    {
        case BoxOrder::ByVolume:
            std::sort(s.remaining_boxes.begin(), s.remaining_boxes.end(),
                      [&](const Box& a, const Box& b)
                      {
                          auto* at = resolve_box_type(a.box_type_id, box_type_map_);
                          auto* bt = resolve_box_type(b.box_type_id, box_type_map_);
                          if (!at || !bt)
                          {
                              return false;
                          }
                          return at->size.volume() > bt->size.volume();
                      });
            break;

        case BoxOrder::ByVolumeAsc:
            std::sort(s.remaining_boxes.begin(), s.remaining_boxes.end(),
                      [&](const Box& a, const Box& b)
                      {
                          auto* at = resolve_box_type(a.box_type_id, box_type_map_);
                          auto* bt = resolve_box_type(b.box_type_id, box_type_map_);
                          if (!at || !bt)
                          {
                              return false;
                          }
                          return at->size.volume() < bt->size.volume();
                      });
            break;

        case BoxOrder::ByHeight:
            std::sort(s.remaining_boxes.begin(), s.remaining_boxes.end(),
                      [&](const Box& a, const Box& b)
                      {
                          auto* at = resolve_box_type(a.box_type_id, box_type_map_);
                          auto* bt = resolve_box_type(b.box_type_id, box_type_map_);
                          if (!at || !bt)
                          {
                              return false;
                          }
                          return at->size.y > bt->size.y;
                      });
            break;

        case BoxOrder::ByPlatformThenVolume:
            std::stable_sort(s.remaining_boxes.begin(), s.remaining_boxes.end(),
                             [&](const Box& a, const Box& b)
                             {
                                 // 空平台排在最后
                                 if (a.platform.empty() != b.platform.empty())
                                 {
                                     return b.platform.empty();
                                 }
                                 // 同平台内按体积降序
                                 auto* at = resolve_box_type(a.box_type_id, box_type_map_);
                                 auto* bt = resolve_box_type(b.box_type_id, box_type_map_);
                                 if (!at || !bt)
                                 {
                                     return false;
                                 }
                                 return at->size.volume() > bt->size.volume();
                             });
            break;

        case BoxOrder::ByMixed:
            // 不排序——调用者会打乱
            break;
    }

    return s;
}

// =============================================================
// construct_solution — 贪心构造式搜索
// =============================================================
bool SolverEngine::construct_solution(SearchState& state)
{
    // 打开第一个容器
    if (!open_new_container(state))
    {
        state.proven_infeasible = true;
        state.failure_reason = "no_container_type_available";
        return false;
    }

    while (!state.remaining_boxes.empty())
    {
        if (!check_time(state))
        {
            return false;
        }

        if (!place_next_box(state))
        {
            // 无法在任何已打开容器中放置剩余箱子
            // 打开新容器前检查 tender_limit 可行性
            if (check_tender_limit_proven_infeasible(state))
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

// =============================================================
// open_new_container — 智能选择容器类型
// =============================================================
bool SolverEngine::open_new_container(SearchState& state)
{
    // 收集可用（未耗尽）的容器类型
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
        auto* bt = resolve_box_type(bx.box_type_id, box_type_map_);
        if (bt)
        {
            remaining_volume += bt->size.volume();
        }
    }

    // 按体积升序排列
    std::sort(available.begin(), available.end(),
              [](const ContainerType* a, const ContainerType* b)
              {
                  return a->inner_size.volume() < b->inner_size.volume();
              });

    // 选择能装下全部剩余体积的最小容器
    // 若均无法装下，则退回最大的可用容器
    const ContainerType* best = available.back();
    for (auto* ct : available)
    {
        if (ct->inner_size.volume() >= remaining_volume)
        {
            // 检查至少一个维度能容纳最大的箱子
            bool dim_ok = false;
            for (const auto& bx : state.remaining_boxes)
            {
                auto* bt = resolve_box_type(bx.box_type_id, box_type_map_);
                if (!bt)
                {
                    continue;
                }

                for (auto o : bt->allowed_orientations)
                {
                    auto os = orient_size(bt->size, o);
                    if (os.dx <= ct->inner_size.x &&
                        os.dy <= ct->inner_size.y &&
                        os.dz <= ct->inner_size.z)
                    {
                        dim_ok = true;
                        break;
                    }
                }
                if (!dim_ok)
                {
                    break;
                }
            }
            if (dim_ok)
            {
                best = ct;
                break;
            }
            best = ct;
        }
    }

    // 创建新容器实例
    ContainerLoad load;
    load.instance_id = "container_" + std::to_string(state.next_container_instance++);
    load.type_id = best->id;
    load.type = &state.container_type_map[best->id];
    load.extreme_points.push_back({0, 0, 0});

    state.open_containers.push_back(std::move(load));
    state.container_type_usage[best->id] =
        (state.container_type_usage[best->id]) + 1;

    return true;
}

// =============================================================
// place_next_box — 选择最优的箱子 × 容器 × 极点 × 朝向组合
// =============================================================
bool SolverEngine::place_next_box(SearchState& state)
{
    struct ScoredCandidate
    {
        size_t box_index;
        Candidate cand;
    };

    std::vector<ScoredCandidate> all_candidates;

    for (size_t i = 0; i < state.remaining_boxes.size(); ++i)
    {
        auto placements = enumerate_placements(state, state.remaining_boxes[i]);
        for (auto& p : placements)
        {
            all_candidates.push_back({i, std::move(p)});
        }
    }

    if (all_candidates.empty())
    {
        return false;
    }

    // 按投影目标排序（最优在前）
    std::sort(all_candidates.begin(), all_candidates.end(),
              [](const ScoredCandidate& a, const ScoredCandidate& b)
              {
                  int cmp = compare_objectives(a.cand.projected_objective,
                                               b.cand.projected_objective);
                  if (cmp != 0)
                  {
                      return cmp < 0;
                  }
                  // 平局 1：优先大箱子（体积利用率）
                  if (a.cand.volume_utilization_delta != b.cand.volume_utilization_delta)
                  {
                      return a.cand.volume_utilization_delta > b.cand.volume_utilization_delta;
                  }
                  // 平局 2：优先紧凑位置（Y, Z, X）
                  const auto& pa = a.cand.position;
                  const auto& pb = b.cand.position;
                  if (pa.y != pb.y)
                  {
                      return pa.y < pb.y;
                  }
                  if (pa.z != pb.z)
                  {
                      return pa.z < pb.z;
                  }
                  return pa.x < pb.x;
              });

    // 应用最优候选
    auto best = all_candidates.front();
    apply_placement(state, best.cand);

    // 从剩余列表中移除已放置的箱子
    state.remaining_boxes.erase(state.remaining_boxes.begin() +
                                static_cast<ptrdiff_t>(best.box_index));

    return true;
}

// =============================================================
// enumerate_placements — 枚举箱子的所有有效放置
// =============================================================
std::vector<Candidate> SolverEngine::enumerate_placements(
    SearchState& state, const Box& box)
{
    auto* bt = resolve_box_type(box.box_type_id, box_type_map_);
    if (!bt)
    {
        return {};
    }

    std::vector<Candidate> candidates;

    for (auto& container : state.open_containers)
    {
        // 极点排序：低 Y（地板）优先，再低 Z（左侧），再低 X（门）
        std::sort(container.extreme_points.begin(), container.extreme_points.end(),
                  [](const Position& a, const Position& b) noexcept
                  {
                      if (a.y != b.y)
                      {
                          return a.y < b.y;
                      }
                      if (a.z != b.z)
                      {
                          return a.z < b.z;
                      }
                      return a.x < b.x;
                  });

        // 平台数量限制预检
        if (problem_.platform_limit.has_value() && !box.platform.empty())
        {
            auto result = check_platform_limit_constraint(
                container, box.platform, problem_.platform_limit.value());
            if (!result.ok)
            {
                continue;
            }
        }

        for (const auto& ep : container.extreme_points)
        {
            for (auto orient : bt->allowed_orientations)
            {
                OrientedSize osize = orient_size(bt->size, orient);

                // 边界检查
                auto boundResult = check_boundary_constraint(container, ep, osize);
                if (!boundResult.ok)
                {
                    continue;
                }

                // 重叠检查
                auto overlapResult = check_overlap_constraint(
                    container, ep, osize, box_type_map_);
                if (!overlapResult.ok)
                {
                    continue;
                }

                // 重量检查
                auto weightResult = check_weight_constraint(container, osize, box.weight);
                if (!weightResult.ok)
                {
                    continue;
                }

                // 支撑检查
                auto supportResult = check_support_constraint(
                    container, ep, osize, problem_.support_rate, box_type_map_);
                if (!supportResult.ok)
                {
                    continue;
                }

                // 路线顺序检查
                if (problem_.route.has_value() && !box.platform.empty())
                {
                    auto routeResult = check_route_order_constraint(
                        container, box.platform, ep, osize, problem_.route.value());
                    if (!routeResult.ok)
                    {
                        continue;
                    }
                }

                // 此放置有效——创建候选
                Candidate cand;
                cand.box_id = box.id;
                cand.container_instance_id = container.instance_id;
                cand.position = ep;
                cand.orientation = orient;
                cand.osize = osize;
                cand.volume_utilization_delta = static_cast<double>(osize.volume());

                bool is_new_platform = !box.platform.empty() &&
                                       !container.platforms.count(box.platform);

                bool group_touches_new_container = false;
                if (!box.group.empty())
                {
                    auto gs_it = state.group_spread.find(box.group);
                    if (gs_it == state.group_spread.end() ||
                        !gs_it->second.count(container.instance_id))
                    {
                        group_touches_new_container = true;
                    }
                }

                cand.projected_objective = project_objective(
                    state.current_objective, container,
                    false, is_new_platform,
                    box.group, group_touches_new_container);

                candidates.push_back(std::move(cand));
            }
        }
    }

    return candidates;
}

// =============================================================
// check_tender_limit_proven_infeasible
// =============================================================
bool SolverEngine::check_tender_limit_proven_infeasible(SearchState& state)
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

        // 统计该组剩余的箱子
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

        // 尝试将每个剩余箱子放到已接触的容器中
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

                if (!cit->extreme_points.empty())
                {
                    can_place = true;
                    break;
                }
            }

            if (!can_place)
            {
                state.proven_infeasible = true;
                state.failure_reason = reason::k_tender_limit;
                return true;
            }
        }
    }

    return false;
}

// =============================================================
// compactify_placement — 将箱子向 (Y-, Z-, X-) 滑动以紧凑排列
// =============================================================
Position SolverEngine::compactify_placement(const ContainerLoad& container,
                                            const Box& box,
                                            Position pos,
                                            const OrientedSize& osize) const
{
    const int32_t step = 1;

    // Y-（重力方向）
    for (int32_t ty = pos.y - step; ty >= 0; ty -= step)
    {
        Position tp{pos.x, ty, pos.z};
        if (check_overlap_any(tp, osize, container.placements, box_type_map_))
        {
            break;
        }
        pos.y = ty;
    }

    // Z-（左壁方向）
    for (int32_t tz = pos.z - step; tz >= 0; tz -= step)
    {
        Position tp{pos.x, pos.y, tz};
        if (check_overlap_any(tp, osize, container.placements, box_type_map_))
        {
            break;
        }
        pos.z = tz;
    }

    // X-（朝向门方向）
    for (int32_t tx = pos.x - step; tx >= 0; tx -= step)
    {
        Position tp{tx, pos.y, pos.z};
        if (check_overlap_any(tp, osize, container.placements, box_type_map_))
        {
            break;
        }
        pos.x = tx;
    }

    return pos;
}

// =============================================================
// apply_placement
// =============================================================
void SolverEngine::apply_placement(SearchState& state, Candidate& cand)
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

    // 紧凑化：向 (Y-, Z-, X-) 滑动以消除间隙
    cand.position = compactify_placement(container, box,
                                         cand.position, cand.osize);

    Placement pl;
    pl.box_id = cand.box_id;
    pl.box_type_id = box.box_type_id;
    pl.container_id = cand.container_instance_id;
    pl.position = cand.position;
    pl.orientation = cand.orientation;

    // 更新容器状态
    container.placements.push_back(pl);
    container.used_volume += cand.osize.volume();
    container.total_weight += box.weight;

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

    // 从此次放置生成新极点
    auto new_eps = generate_extreme_points(cand.position, cand.osize, container);
    container.extreme_points.insert(
        container.extreme_points.end(),
        new_eps.begin(), new_eps.end());

    filter_extreme_points(container.extreme_points, container, box_type_map_);

    // 更新当前目标缓存
    state.current_objective = compute_objective(state.open_containers);
}

// =============================================================
// check_time
// =============================================================
bool SolverEngine::check_time(const SearchState& state) const
{
    auto elapsed = std::chrono::steady_clock::now() - state.start_time;
    auto elapsed_sec = std::chrono::duration<double>(elapsed).count();
    return elapsed_sec < state.time_limit_seconds;
}

// =============================================================
// update_best
// =============================================================
void SolverEngine::update_best(SearchState& state)
{
    if (!state.remaining_boxes.empty())
    {
        return;
    }

    auto ov = compute_objective(state.open_containers);

    if (!state.best_feasible.has_value())
    {
        state.best_feasible = build_solution(state, Status::Success, reason::k_feasible);
        state.best_feasible->objective = ov;
    }
    else
    {
        if (ov.is_better_than(state.best_feasible->objective.value_or(ObjectiveVector{})))
        {
            state.best_feasible = build_solution(state, Status::Success, reason::k_feasible);
            state.best_feasible->objective = ov;
        }
    }
}

// =============================================================
// build_solution
// =============================================================
Solution SolverEngine::build_solution(const SearchState& state,
                                      Status status,
                                      const std::string& reason) const
{
    Solution sol;
    sol.status = status;
    sol.reason = reason;

    auto elapsed = std::chrono::steady_clock::now() - state.start_time;
    sol.elapsed_second = std::chrono::duration<double>(elapsed).count();

    sol.container_count = static_cast<int>(state.open_containers.size());
    sol.packed_box_count = static_cast<int>(
        problem_.boxes.size() - state.remaining_boxes.size());
    sol.unpacked_box_count = static_cast<int>(state.remaining_boxes.size());

    sol.objective = compute_objective(state.open_containers);

    for (const auto& load : state.open_containers)
    {
        ContainerSummary cs;
        cs.id = load.instance_id;
        cs.type_id = load.type_id;
        cs.used_volume = load.used_volume;
        cs.total_volume = load.total_volume();
        cs.volume_rate = load.volume_rate();
        cs.total_weight = load.total_weight;
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

    return sol;
}

// =============================================================
// multi_start_solve — 尝试不同排序 + 打乱
// =============================================================
void SolverEngine::multi_start_solve(SearchState& state)
{
    std::mt19937 rng(static_cast<unsigned>(state.config ? state.config->random_seed : 42));

    const BoxOrder orders[] = {
        BoxOrder::ByVolume,
        BoxOrder::ByVolumeAsc,
        BoxOrder::ByHeight,
        BoxOrder::ByPlatformThenVolume,
        BoxOrder::ByMixed,
    };

    for (auto order : orders)
    {
        if (!check_time(state))
        {
            break;
        }

        int seeds = (order == BoxOrder::ByMixed) ? 3 : 2;
        for (int s = 0; s < seeds; ++s)
        {
            if (!check_time(state))
            {
                break;
            }

            SearchState fresh = make_initial_state(order);
            fresh.start_time = state.start_time;
            fresh.time_limit_seconds = state.time_limit_seconds;

            if (order == BoxOrder::ByMixed)
            {
                std::shuffle(fresh.remaining_boxes.begin(),
                             fresh.remaining_boxes.end(), rng);
            }

            bool all_packed = construct_solution(fresh);

            if (all_packed && fresh.best_feasible.has_value())
            {
                auto cand_ov = compute_objective(fresh.open_containers);

                if (!state.best_feasible.has_value() ||
                    cand_ov.is_better_than(
                        state.best_feasible->objective.value_or(ObjectiveVector{})))
                {
                    Solution& cand = fresh.best_feasible.value();
                    state.best_feasible = std::move(cand);
                    state.best_feasible->objective = cand_ov;
                }

                // 单容器 -> min_container_count 已达到最优
                if (fresh.open_containers.size() == 1)
                {
                    return;
                }
            }
        }
    }
}

} // namespace hypercube
