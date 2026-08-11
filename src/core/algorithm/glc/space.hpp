#pragma once

#include <cstdint>
#include <vector>

#include "../../types.hpp"

namespace pack3d::glc
{

enum class SpaceKind : uint8_t
{
    Root,
    Z,
    X,
    Y,
};

// 剩余空间（轴对齐长方体）
struct Space
{
    Position pos;
    int32_t lx = 0, ly = 0, lz = 0;
    int64_t id = 0;
    int64_t parent_id = -1;
    SpaceKind kind = SpaceKind::Root;
};

/// 放置一个块后，将剩余空间确定性划分为 3 个子空间
/// 入栈顺序取决于 (lx, ly) 的大小关系，优先让"更容易利用"的空间在栈顶
void split_space(const Space& space, const OrientedSize& block_osize,
                 std::vector<Space>& stack) noexcept;

/// 尝试将栈顶空间的可转移部分合并给兄弟空间
/// 如果合并成功，原空间被弹出，兄弟空间被替换
/// 返回 true 表示空间被回收
bool transfer_space(std::vector<Space>& stack) noexcept;

/// 从 Space 中挖掉嵌入的放置（不在角落时用），6-slab 完整分解剩余空间入栈（不丢对角空间）
void carve_out_space(const Space& space,
                     const Placement& pl,
                     std::vector<Space>& stack) noexcept;

/// 从空间栈中挖掉全部障碍物（每个障碍物对每个相交空间 6 向切割）
void carve_obstacles(std::vector<Space>& stack,
                     const std::vector<Obstacle>& obstacles) noexcept;

/// 阶梯雕刻"覆盖原点"的斜面贴角楔形（两负截距；其余斜面不雕刻、由 check_facet 兜底）
void carve_facets(std::vector<Space>& stack,
                  const Size& container_size,
                  const std::vector<Facet>& facets) noexcept;

} // namespace pack3d::glc
