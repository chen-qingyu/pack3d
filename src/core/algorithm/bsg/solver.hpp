#pragma once

#include "types.hpp"

namespace pack3d::bsg
{

/// BSG-CLP 主入口 (Algorithm 1)
/// 对单容器 + 箱子集合运行 beam search，返回装箱结果
PackResult solve(const GlobalContext& ctx,
                 const std::vector<int>& initial_counts,
                 const std::vector<std::vector<std::string>>& box_ids_by_type,
                 double time_limit_sec);

} // namespace pack3d::bsg
