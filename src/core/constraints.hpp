#pragma once

#include <map>
#include <string>
#include <vector>

#include "types.hpp"

namespace hypercube
{

/// 检查箱子是否完全在容器边界内
[[nodiscard]] bool check_boundary(const ContainerType& ctype, const Position& pos,
                                  const OrientedSize& osize) noexcept;

/// 检查新放置是否与已有放置重叠
[[nodiscard]] bool check_overlap(const Position& pos, const OrientedSize& osize,
                                 const std::vector<Placement>& existing) noexcept;

/// 检查放入箱子后是否超重
[[nodiscard]] bool check_weight(const ContainerLoad& load,
                                double box_weight) noexcept;

/// 检查底面支撑率是否达标（support_rate=0 则跳过）
[[nodiscard]] bool check_support(const Position& pos, const OrientedSize& osize,
                                 const ContainerLoad& load,
                                 const std::map<std::string, BoxType>& box_type_map,
                                 double support_rate) noexcept;

/// 平台数量限制预检
[[nodiscard]] bool check_platform_limit(const ContainerLoad& load,
                                        const std::string& platform,
                                        int platform_limit) noexcept;

/// 路线顺序约束：先装平台（idx 小）应在更深处（X 小），后装平台应在更近门处（X 大）
[[nodiscard]] bool check_route_order(const ContainerLoad& load,
                                     const std::string& platform,
                                     const Position& pos, const OrientedSize& osize,
                                     const RouteOrder& route) noexcept;

} // namespace hypercube
