#pragma once

#include <cstdint>
#include <vector>

#include "types.hpp"

namespace pack3d::bsg
{

/// Greedy rollout 结果
struct GreedyResult
{
    int64_t total_volume = 0;         // 最终装箱总体积
    int remaining_platform_count = 0; // rollout 结束后仍有剩余箱子的平台数（平台拆分偏好的并列评分）
    std::vector<int> packed_counts;   // 每箱型装了几个（用于去相似 hash）
    BSGState final_state;             // rollout 结束时的完整状态
};

/// 统计 state 中仍有剩余箱子的平台数（空平台不计）
int count_remaining_platforms(const BSGState& state, const GlobalContext& ctx);

/// Greedy rollout (Algorithm 2 中的 greedy 过程)
/// 从部分状态开始，贪心放置块直到无法继续
/// 返回最终装箱总体积和每箱型装箱数
///
/// 注意：此函数不修改传入的 state（拷贝后操作）
GreedyResult greedy_rollout(
    const BSGState& state,
    const GlobalContext& ctx);

} // namespace pack3d::bsg
