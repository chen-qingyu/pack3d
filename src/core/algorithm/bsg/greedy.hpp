#pragma once

#include <cstdint>
#include <vector>

#include "types.hpp"

namespace pack3d::bsg
{

/// Greedy rollout 结果
struct GreedyResult
{
    int64_t total_volume = 0;       // 最终装箱总体积
    std::vector<int> packed_counts; // 每箱型装了几个（用于去相似 hash）
    BSGState final_state;           // rollout 结束时的完整状态
};

/// Greedy rollout (Algorithm 2 中的 greedy 过程)
/// 从部分状态开始，贪心放置块直到无法继续
/// s_best_volume: 当前全局最优完整解的总体积，用于剪枝（0 表示无剪枝）
/// 返回最终装箱总体积和每箱型装箱数
///
/// 注意：此函数不修改传入的 state（拷贝后操作）
GreedyResult greedy_rollout(
    const BSGState& state,
    int64_t s_best_volume,
    const GlobalContext& ctx);

/// 计算剩余箱子最大可能体积（剪枝用上界）
int64_t max_remaining_volume(const BSGState& state, const GlobalContext& ctx);

} // namespace pack3d::bsg
