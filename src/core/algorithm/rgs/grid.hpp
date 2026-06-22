#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "../../types.hpp"
#include "state.hpp"

namespace hypercube::rgs
{

// 按论文 §4.4.4：平均边长作为 cell_size
[[nodiscard]] int32_t compute_cell_size(
    const std::vector<Box>& boxes,
    const std::map<std::string, BoxType>& box_type_map) noexcept;

// 将 placement 注册到网格
void grid_register(
    EpContext& ctx,
    const std::vector<Placement>& placements,
    size_t placement_index) noexcept;

// 查询候选放置区域覆盖的网格单元中的 placement 索引
[[nodiscard]] std::vector<size_t> grid_neighbors(
    const EpContext& ctx,
    const std::vector<Placement>& placements,
    const Position& pos,
    const OrientedSize& osize) noexcept;

// AABB 碰撞检测：候选放置是否与 neighbors 中任一 placement 重叠
[[nodiscard]] bool grid_collides(
    const std::vector<Placement>& placements,
    const Position& pos,
    const OrientedSize& osize,
    const std::vector<size_t>& neighbors) noexcept;

} // namespace hypercube::rgs
