#pragma once

#include <cstdint>
#include <vector>

#include "../../types.hpp"

namespace hypercube::glc
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

} // namespace hypercube::glc
