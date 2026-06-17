#include "placer.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

#include <spdlog/spdlog.h>

#include "../../constraints.hpp"
#include "../../geometry.hpp"
#include "../../objectives.hpp"

namespace hypercube::sgep
{

Placer::Placer(const std::map<std::string, BoxType>& box_type_map,
               const Problem& problem,
               bool has_weight_info)
    : box_type_map_(box_type_map)
    , problem_(problem)
    , has_weight_info_(has_weight_info)
{
}

// 按目标投影选择最优放置
bool Placer::place_next_box(SearchState& state)
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
            auto& eps = state.extreme_points[container.instance_id];
            std::sort(eps.begin(), eps.end(),
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
                if (!check_platform_limit_constraint(
                        container, box.platform, problem_.platform_limit.value()))
                {
                    continue;
                }
            }

            size_t ep_limit = std::min(eps.size(), size_t(200));
            for (size_t ei = 0; ei < ep_limit; ++ei)
            {
                const auto& ep = eps[ei];
                for (auto orient : bt.allowed_orientations)
                {
                    OrientedSize osize = orient_size(bt.size, orient);

                    if (!check_boundary_constraint(container, ep, osize))
                    {
                        continue;
                    }
                    if (!check_overlap_constraint(container, ep, osize, box_type_map_))
                    {
                        continue;
                    }
                    if (has_weight_info_ && !check_weight_constraint(container, osize, box.weight.value()))
                    {
                        continue;
                    }
                    if (!check_support_constraint(container, ep, osize, problem_.support_rate, box_type_map_))
                    {
                        continue;
                    }

                    if (problem_.route.has_value() && !box.platform.empty())
                    {
                        if (!check_route_order_constraint(
                                container, box.platform, ep, osize, problem_.route.value()))
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
            state.extreme_points[load.instance_id].push_back({0, 0, 0});
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

void Placer::apply_placement(SearchState& state, Candidate& cand)
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

    Placement pl;
    pl.box_id = cand.box_id;
    pl.box_type_id = box.box_type_id;
    pl.container_id = cand.container_instance_id;
    pl.position = cand.position;
    pl.orientation = cand.orientation;
    pl.platform = box.platform;
    pl.group = box.group;

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

    auto& eps = state.extreme_points[container.instance_id];
    auto new_eps = generate_extreme_points(cand.position, cand.osize, container);
    eps.insert(eps.end(), new_eps.begin(), new_eps.end());

    filter_extreme_points(eps, container, box_type_map_);

    state.current_objective = compute_objective(state.open_containers);
}

} // namespace hypercube::sgep
