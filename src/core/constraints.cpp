#include "constraints.hpp"

#include <algorithm>
#include <cmath>

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

} // namespace hypercube
