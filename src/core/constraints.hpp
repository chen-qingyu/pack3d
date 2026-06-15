#pragma once

#include <string>
#include <vector>

#include "geometry.hpp"
#include "types.hpp"

namespace hypercube
{

[[nodiscard]] bool check_boundary_constraint(const ContainerLoad& load,
                                             const Position& pos,
                                             const OrientedSize& osize) noexcept;

[[nodiscard]] bool check_overlap_constraint(
    const ContainerLoad& load,
    const Position& pos, const OrientedSize& osize,
    const std::map<std::string, BoxType>& box_type_map) noexcept;

[[nodiscard]] bool check_weight_constraint(const ContainerLoad& load,
                                           const OrientedSize& osize,
                                           double box_weight) noexcept;

[[nodiscard]] bool check_support_constraint(
    const ContainerLoad& load,
    const Position& pos, const OrientedSize& osize,
    double support_rate,
    const std::map<std::string, BoxType>& box_type_map) noexcept;

/// 路线 X 顺序约束（装货顺序）：先装平台（idx 小）应在更深处（X 小），后装平台（idx 大）应在更近门处（X 大）
[[nodiscard]] bool check_route_order_constraint(
    const ContainerLoad& load,
    const std::string& platform,
    const Position& pos, const OrientedSize& osize,
    const RouteOrder& route) noexcept;

/// 平台数量限制约束（预检）
[[nodiscard]] bool check_platform_limit_constraint(
    const ContainerLoad& load,
    const std::string& platform,
    int platform_limit) noexcept;

} // namespace hypercube
