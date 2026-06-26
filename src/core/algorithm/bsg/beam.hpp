#pragma once

#include <cstdint>

#include "types.hpp"

namespace hypercube::bsg
{

/// BeamSearch (Algorithm 2)
/// 返回本轮找到的最优完整解的总体积
/// s_best_volume: 全局最优体积（in-out）
/// s_best: 全局最优完整状态（in-out），被更新当 greedy 找到更优解
int64_t beam_search(
    BSGState s0,
    int w,
    int64_t& s_best_volume,
    BSGState& s_best,
    const GlobalContext& ctx);

} // namespace hypercube::bsg
