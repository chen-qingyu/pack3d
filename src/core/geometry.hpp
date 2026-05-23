#pragma once

#include <vector>

#include "types.hpp"

namespace hypercube
{

// 几何辅助函数

/// 应用朝向到基础尺寸
[[nodiscard]] OrientedSize orient_size(const Size& base, Orientation o) noexcept;

/// 检查箱子是否完全在容器边界内
[[nodiscard]] bool check_boundary(const ContainerType& ctype, const Position& pos,
                                  const OrientedSize& osize) noexcept;

/// 检查两个有朝向的箱子是否在 3D 空间重叠
[[nodiscard]] bool check_overlap(const Position& a_pos, const OrientedSize& a_size,
                                 const Position& b_pos, const OrientedSize& b_size) noexcept;

/// 检查新放置是否与任何已有放置重叠
[[nodiscard]] bool check_overlap_any(const Position& pos, const OrientedSize& osize,
                                     const std::vector<Placement>& existing,
                                     const std::map<std::string, BoxType>& box_type_map) noexcept;

/// 计算支撑率（底面面积中由其他箱子或地板支撑的比例）
[[nodiscard]] double calc_support_ratio(const Position& pos, const OrientedSize& osize,
                                        const ContainerLoad& load,
                                        const std::map<std::string, BoxType>& box_type_map) noexcept;

// 极点生成

/// 放置箱子后生成候选极点（无序，可能包含重复）
[[nodiscard]] std::vector<Position> generate_extreme_points(
    const Position& pos, const OrientedSize& osize,
    const ContainerLoad& load) noexcept;

/// 过滤极点：移除在已有箱子内部或超出容器边界的点
void filter_extreme_points(std::vector<Position>& points,
                           const ContainerLoad& load,
                           const std::map<std::string, BoxType>& box_type_map) noexcept;

// 查找辅助

/// 构建 box_type_id -> BoxType 查找映射
[[nodiscard]] std::map<std::string, BoxType> build_box_type_map(const std::vector<BoxType>& types) noexcept;

/// 构建 container_type_id -> ContainerType 查找映射
[[nodiscard]] std::map<std::string, ContainerType> build_container_type_map(
    const std::vector<ContainerType>& types) noexcept;

} // namespace hypercube
