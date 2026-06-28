#pragma once

#include <vector>

#include "types.hpp"

namespace pack3d::bsg
{

// ============================================================
// Cover representation 操作 (K1)
// ============================================================

/// 计算 cuboid r 的 anchor corner（曼哈顿距离最小的角，K3/K5）
/// 返回：anchor corner 坐标（块将以此为基准放置）
Position anchor_corner(const Cuboid& r, int32_t container_lx) noexcept;

/// 计算块 b 在 cuboid r 中的放置位置（min corner），以 anchor corner 对齐
Position placement_position(const Cuboid& r, const GeneralBlock& b,
                            int32_t container_lx) noexcept;

/// 在残差空间 R 中放置一个块，更新 cover representation
/// block_pos: 块的 min corner 坐标
/// block_size: 块的外包尺寸
void update_residual_space(
    std::vector<Cuboid>& R,
    const Position& block_pos,
    const OrientedSize& block_size);

/// Non-maximal cuboid 剔除
/// 移除所有被另一个 cuboid 完全包含的 cuboid
void remove_non_maximal(std::vector<Cuboid>& R) noexcept;

} // namespace pack3d::bsg
