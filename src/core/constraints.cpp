#include "constraints.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace hypercube
{

// 边界约束
ConstraintResult check_boundary_constraint(const ContainerLoad& load,
                                           const Position& pos,
                                           const OrientedSize& osize) noexcept
{
    if (!check_boundary(*load.type, pos, osize))
    {
        return {false, Violation{"boundary", {}, "box_exceeds_container_boundaries"}};
    }
    return {true, std::nullopt};
}

// 重叠约束
ConstraintResult check_overlap_constraint(
    const ContainerLoad& load,
    const Position& pos, const OrientedSize& osize,
    const std::map<std::string, BoxType>& box_type_map) noexcept
{
    if (check_overlap_any(pos, osize, load.placements, box_type_map))
    {
        return {false, Violation{"overlap", {}, "box_overlaps_existing_boxes"}};
    }
    return {true, std::nullopt};
}

// 重量约束
ConstraintResult check_weight_constraint(const ContainerLoad& load,
                                         const OrientedSize& osize,
                                         double box_weight) noexcept
{
    if (!load.type->max_weight.has_value())
    {
        return {true, std::nullopt};
    }
    if (load.total_weight + box_weight > load.type->max_weight.value() + 1e-9)
    {
        return {false, Violation{"weight", {}, "container_weight_limit_exceeded"}};
    }
    return {true, std::nullopt};
}

// 支撑率约束
ConstraintResult check_support_constraint(
    const ContainerLoad& load,
    const Position& pos, const OrientedSize& osize,
    double support_rate,
    const std::map<std::string, BoxType>& box_type_map) noexcept
{
    // support_rate == 0 表示跳过检查
    if (support_rate <= 0.0)
    {
        return {true, std::nullopt};
    }

    double ratio = calc_support_ratio(pos, osize, load, box_type_map);
    if (ratio + 1e-9 < support_rate)
    {
        return {false, Violation{"support", {}, "insufficient_bottom_support"}};
    }
    return {true, std::nullopt};
}

// 路线顺序约束
ConstraintResult check_route_order_constraint(
    const ContainerLoad& load,
    const std::string& platform,
    const Position& pos, const OrientedSize& osize,
    const RouteOrder& route) noexcept
{
    // 无平台或无路线 -> 跳过
    if (platform.empty())
    {
        return {true, std::nullopt};
    }

    auto it = route.index_of.find(platform);
    if (it == route.index_of.end())
    {
        return {true, std::nullopt};
    }

    size_t my_idx = it->second;

    // 装货顺序：先装的在深处（X 小），后装的在近门处（X 大），允许并列或堆叠
    for (const auto& [other_plat, other_min_x] : load.platform_x_min)
    {
        if (other_plat == platform)
        {
            continue;
        }
        auto oit = route.index_of.find(other_plat);
        if (oit == route.index_of.end())
        {
            continue;
        }

        size_t other_idx = oit->second;

        if (my_idx > other_idx)
        {
            // 我后装对方先装：我的箱子不能比对方更靠里
            if (pos.x < other_min_x)
            {
                return {false, Violation{"route_order", {platform, other_plat}, "route_order_violation"}};
            }
        }
        else
        {
            // 我先装对方后装：我的箱子不能比对方更靠近门
            auto max_it = load.platform_x_max.find(other_plat);
            if (max_it != load.platform_x_max.end())
            {
                if (pos.x + osize.dx > max_it->second)
                {
                    return {false, Violation{"route_order", {platform, other_plat}, "route_order_violation"}};
                }
            }
        }
    }

    return {true, std::nullopt};
}

// 平台数量限制约束
ConstraintResult check_platform_limit_constraint(
    const ContainerLoad& load,
    const std::string& platform,
    int platform_limit) noexcept
{
    if (platform_limit <= 0)
    {
        return {true, std::nullopt};
    }
    // 平台已存在，此次放置不会增加计数
    if (load.platforms.count(platform))
    {
        return {true, std::nullopt};
    }
    // 若引入新平台会超出限制
    if (static_cast<int>(load.platforms.size()) >= platform_limit)
    {
        return {false, Violation{"platform_limit", {platform}, "container_would_exceed_platform_limit"}};
    }
    return {true, std::nullopt};
}

// 最终解检查：对求解器输出的解进行全面验证，返回违规列表（空表示通过）
std::vector<Violation> final_check_solution(
    const Solution& solution,
    const Problem& problem,
    const std::map<std::string, BoxType>& box_type_map,
    const std::map<std::string, Box>& box_map,
    bool has_weight_info) noexcept
{
    std::vector<Violation> out;
    std::set<std::string> all_placed_boxes;

    for (size_t ci = 0; ci < solution.container_summaries.size(); ++ci)
    {
        const auto& summary = solution.container_summaries[ci];
        const auto& placements = solution.container_placements[ci];

        // 查找容器类型
        const ContainerType* ctype = nullptr;
        for (const auto& ct : problem.container_types)
        {
            if (ct.id == summary.type_id)
            {
                ctype = &ct;
                break;
            }
        }
        if (ctype == nullptr)
        {
            out.push_back({"internal", {summary.id}, "unknown_container_type"});
            continue;
        }

        ContainerLoad load;
        load.instance_id = summary.id;
        load.type_id = summary.type_id;
        load.type = ctype;

        double total_w = 0;

        for (const auto& pl : placements)
        {
            // 箱子唯一性检查
            if (!all_placed_boxes.insert(pl.box_id).second)
            {
                out.push_back({"duplicate_box", {pl.box_id}, "final_duplicate_box_violation"});
            }

            auto& bt = box_type_map.at(pl.box_type_id);
            auto osize = orient_size(bt.size, pl.orientation);

            // 查找箱子信息
            auto bit = box_map.find(pl.box_id);
            double box_weight = (bit != box_map.end()) ? bit->second.weight.value_or(0.0) : 0.0;
            total_w += box_weight;

            std::string box_platform = (bit != box_map.end()) ? bit->second.platform : std::string();

            // 边界检查
            if (!check_boundary(*ctype, pl.position, osize))
            {
                out.push_back({"boundary", {pl.box_id}, "final_boundary_violation"});
            }

            // 与该容器内已处理放置的重叠检查
            for (const auto& prev : load.placements)
            {
                auto& pbt = box_type_map.at(prev.box_type_id);
                auto psize = orient_size(pbt.size, prev.orientation);
                if (check_overlap(prev.position, psize, pl.position, osize))
                {
                    out.push_back({"overlap", {pl.box_id, prev.box_id}, "final_overlap_violation"});
                }
            }

            // 支撑率检查
            if (problem.support_rate > 0.0)
            {
                double ratio = calc_support_ratio(pl.position, osize, load, box_type_map);
                if (ratio + 1e-9 < problem.support_rate)
                {
                    out.push_back({"support", {pl.box_id}, "final_insufficient_support"});
                }
            }

            // 平台数量限制检查
            if (!box_platform.empty() && problem.platform_limit.has_value() && problem.platform_limit.value() > 0)
            {
                auto pr = check_platform_limit_constraint(load, box_platform, problem.platform_limit.value());
                if (!pr.ok)
                {
                    out.push_back({"platform_limit", {pl.box_id}, "final_platform_limit_violation"});
                }
            }

            // 路线顺序检查（装货顺序）：先装的在深处（X 小），后装的在近门处（X 大）
            // 允许并列（同 X 不同 Y）和堆叠（同 X 不同 Z）
            if (!box_platform.empty() && problem.route.has_value())
            {
                const auto& route = problem.route.value();
                auto my_it = route.index_of.find(box_platform);
                if (my_it != route.index_of.end())
                {
                    size_t my_idx = my_it->second;
                    int32_t box_min_x = pl.position.x;
                    for (const auto& [other_plat, other_min_x] : load.platform_x_min)
                    {
                        auto oit = route.index_of.find(other_plat);
                        if (oit == route.index_of.end())
                            continue;
                        if (oit->second >= my_idx)
                            continue; // 对方路线不更早
                        if (box_min_x < other_min_x)
                        {
                            out.push_back({"route_order", {pl.box_id}, "final_route_order_violation"});
                        }
                    }
                }
            }

            // 更新容器跟踪状态（供后续支撑率 / 路线 / 平台检查使用）
            load.placements.push_back(pl);
            if (!box_platform.empty())
            {
                load.platforms.insert(box_platform);

                int32_t box_min_x = pl.position.x;
                int32_t box_max_x = pl.position.x + osize.dx;

                auto xmax_it = load.platform_x_max.find(box_platform);
                if (xmax_it == load.platform_x_max.end() || box_max_x > xmax_it->second)
                {
                    load.platform_x_max[box_platform] = box_max_x;
                }

                auto xmin_it = load.platform_x_min.find(box_platform);
                if (xmin_it == load.platform_x_min.end() || box_min_x < xmin_it->second)
                {
                    load.platform_x_min[box_platform] = box_min_x;
                }
            }
        }

        // 重量检查
        if (has_weight_info && ctype->max_weight.has_value() && total_w > ctype->max_weight.value() + 1e-9)
        {
            out.push_back({"weight", {summary.id}, "final_weight_violation"});
        }
    }

    return out;
}

} // namespace hypercube
