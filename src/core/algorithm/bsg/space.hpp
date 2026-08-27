#pragma once

#include <vector>

#include "types.hpp"

namespace pack3d::bsg
{

// ============================================================
// Cover representation 操作 (K1)
// ============================================================

/// 多 cuboid 展开限界：按体积降序取最多 max_cuboids 个残差空间索引（并列按索引升序）。
/// 残差空间碎片很多时避免 O(C×B) 全局扫描过慢；大 cuboid 才是能装下块的有效空间。
std::vector<size_t> top_cuboids_by_volume(const std::vector<Cuboid>& spaces,
                                          size_t max_cuboids);

/// 单个 cuboid 的最佳 anchor 及对应距离（K3/K5）。
/// route_aware：X 固定 min-X（深角），Y/Z 仍取最近壁面（使平台从深往门装载）；
/// 否则标准 Manhattan 最近壁面角。
struct AnchorPick
{
    Position anchor;
    int32_t distance = 0;
};
AnchorPick best_anchor_for(const Cuboid& r,
                           const Size& container_size,
                           bool route_aware) noexcept;

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
