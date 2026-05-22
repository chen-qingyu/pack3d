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
    if (load.type == nullptr)
    {
        return {false, Violation{"internal", {}, "no_container_type"}};
    }
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
    if (load.type == nullptr)
    {
        return {false, Violation{"internal", {}, "no_container_type"}};
    }
    if (load.total_weight + box_weight > load.type->max_weight + 1e-9)
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

    // 检查与容器内其他每个平台的相对位置
    for (const auto& [other_plat, other_max_x] : load.platform_x_max)
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

        if (my_idx < other_idx)
        {
            // 我比对方先装载 -> 我应该更深（X 更大）
            if (pos.x + osize.dx <= other_max_x)
            {
                return {false, Violation{"route_order", {platform, other_plat}, "route_order_violation"}};
            }
        }
        else
        {
            // 我比对方后装载 -> 我应该更浅（X 更小）
            auto min_it = load.platform_x_min.find(other_plat);
            if (min_it != load.platform_x_min.end())
            {
                if (pos.x >= min_it->second)
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
    const std::map<std::string, Box>& box_map) noexcept
{
    std::vector<Violation> out;

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
            auto* bt = resolve_box_type(pl.box_type_id, box_type_map);
            if (bt == nullptr)
            {
                out.push_back({"internal", {pl.box_id}, "unknown_box_type_in_placement"});
                continue;
            }
            auto osize = orient_size(bt->size, pl.orientation);

            // 查找箱子重量
            auto bit = box_map.find(pl.box_id);
            double box_weight = (bit != box_map.end()) ? bit->second.weight : 0.0;
            total_w += box_weight;

            // 边界检查
            if (!check_boundary(*ctype, pl.position, osize))
            {
                out.push_back({"boundary", {pl.box_id}, "final_boundary_violation"});
            }

            // 与该容器内已处理放置的重叠检查
            for (const auto& prev : load.placements)
            {
                auto* pbt = resolve_box_type(prev.box_type_id, box_type_map);
                if (pbt == nullptr)
                {
                    continue;
                }
                auto psize = orient_size(pbt->size, prev.orientation);
                if (check_overlap(prev.position, psize, pl.position, osize))
                {
                    out.push_back({"overlap", {pl.box_id, prev.box_id}, "final_overlap_violation"});
                }
            }

            load.placements.push_back(pl);
        }

        // 重量检查
        if (total_w > ctype->max_weight + 1e-9)
        {
            out.push_back({"weight", {summary.id}, "final_weight_violation"});
        }
    }

    return out;
}

} // namespace hypercube
