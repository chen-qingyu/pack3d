#include "constraints.hpp"

#include <algorithm>
#include <cmath>

namespace hypercube
{

ConstraintResult check_boundary_constraint(const ContainerLoad& load,
                                           const Position& pos,
                                           const OrientedSize& osize) noexcept
{
    return {check_boundary(*load.type, pos, osize)};
}

ConstraintResult check_overlap_constraint(
    const ContainerLoad& load,
    const Position& pos, const OrientedSize& osize,
    const std::map<std::string, BoxType>& box_type_map) noexcept
{
    return {!check_overlap_any(pos, osize, load.placements, box_type_map)};
}

ConstraintResult check_weight_constraint(const ContainerLoad& load,
                                         const OrientedSize& osize,
                                         double box_weight) noexcept
{
    if (!load.type->max_weight.has_value())
    {
        return {true};
    }
    return {load.total_weight + box_weight <= load.type->max_weight.value() + 1e-9};
}

ConstraintResult check_support_constraint(
    const ContainerLoad& load,
    const Position& pos, const OrientedSize& osize,
    double support_rate,
    const std::map<std::string, BoxType>& box_type_map) noexcept
{
    if (support_rate <= 0.0)
    {
        return {true};
    }
    double ratio = calc_support_ratio(pos, osize, load, box_type_map);
    return {ratio + 1e-9 >= support_rate};
}

ConstraintResult check_route_order_constraint(
    const ContainerLoad& load,
    const std::string& platform,
    const Position& pos, const OrientedSize& osize,
    const RouteOrder& route) noexcept
{
    if (platform.empty())
    {
        return {true};
    }

    auto it = route.index_of.find(platform);
    if (it == route.index_of.end())
    {
        return {true};
    }

    size_t my_idx = it->second;

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
            if (pos.x < other_min_x)
            {
                return {false};
            }
        }
        else
        {
            auto max_it = load.platform_x_max.find(other_plat);
            if (max_it != load.platform_x_max.end())
            {
                if (pos.x + osize.dx > max_it->second)
                {
                    return {false};
                }
            }
        }
    }

    return {true};
}

ConstraintResult check_platform_limit_constraint(
    const ContainerLoad& load,
    const std::string& platform,
    int platform_limit) noexcept
{
    if (platform_limit <= 0)
    {
        return {true};
    }
    if (load.platforms.count(platform))
    {
        return {true};
    }
    return {static_cast<int>(load.platforms.size()) < platform_limit};
}

} // namespace hypercube
