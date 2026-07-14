#pragma once

#include <vector>

#include "types.hpp"

namespace pack3d::bsg
{

struct SpaceSelection
{
    size_t cuboid_index = 0;
    Position anchor;
    int32_t distance = 0;
};

// ============================================================
// Cover representation 操作 (K1)
// ============================================================

/// 根据八个对应角的 Manhattan 距离选择残差空间和 anchor（K3/K5）。
/// 距离并列时选择体积更大的残差空间。
SpaceSelection select_free_space(const std::vector<Cuboid>& spaces,
                                 const Size& container_size) noexcept;

/// 计算块 b 在 cuboid r 中的放置位置（min corner），以 anchor corner 对齐
Position placement_position(const Cuboid& r, const GeneralBlock& b,
                            const Position& anchor) noexcept;

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
