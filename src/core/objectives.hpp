#pragma once

#include "types.hpp"

namespace pack3d
{

// 目标向量计算与比较

/// 根据 ContainerLoad 列表计算完整目标向量
[[nodiscard]] ObjectiveVector compute_objective(
    const std::vector<ContainerLoad>& containers) noexcept;

// 字典序比较辅助

/// 比较两个目标向量，按默认目标键顺序进行字典序比较
/// 返回 -1 若 a < b（a 更优），0 若相等，1 若 a > b（b 更优）
[[nodiscard]] int compare_objectives(const ObjectiveVector& a,
                                     const ObjectiveVector& b) noexcept;

} // namespace pack3d
