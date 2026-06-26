#pragma once

#include <vector>

#include "types.hpp"

namespace hypercube::bsg
{

/// Expand 过程 (Algorithm 3)
/// 从 state s 扩展出 w 个后继状态（根节点用 w²）
/// s 的 KPA 必须已经计算过
/// 返回后继状态列表（可能少于 w，如果没有足够多可行块）
std::vector<BSGState> expand(
    const BSGState& s,
    int w,
    const GlobalContext& ctx);

} // namespace hypercube::bsg
