#pragma once

#include "types.hpp"

namespace pack3d::bsg
{

/// KPA (Knapsack Problem Algorithm) — K4 的 V_loss 计算基础
/// 对给定 state，计算三轴最大线性延伸并缓存到 state.kpa_L/W/H
void run_kpa(BSGState& state, const GlobalContext& ctx);

/// 计算 V_loss(b, r) 和 f(b, r)
/// 要求 state 的 KPA 已经计算过
int64_t compute_f(const BSGState& state, const Cuboid& r,
                  const GeneralBlock& b, const GlobalContext& ctx);

/// V_loss 单独计算（供调试）
int64_t compute_v_loss(const BSGState& state, const Cuboid& r,
                       const GeneralBlock& b, const GlobalContext& ctx);

} // namespace pack3d::bsg
