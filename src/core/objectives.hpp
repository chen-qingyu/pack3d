#pragma once

#include <string>
#include <vector>

#include "types.hpp"

namespace hypercube
{

// =============================================================
// 目标向量计算与比较
// =============================================================

/// 默认目标键（按优先级排序）
[[nodiscard]] inline const std::vector<std::string>& default_objective_keys()
{
    static const std::vector<std::string> keys = {
        "min_container_count",
        "min_platforms_per_container",
        "max_avg_volume_rate",
        "min_group_split",
    };
    return keys;
}

/// 解析有效的目标键列表（使用用户提供或默认值）
[[nodiscard]] std::vector<std::string> resolve_objective_keys(
    const std::vector<std::string>& user_keys) noexcept;

/// 根据 ContainerLoad 列表计算完整目标向量
[[nodiscard]] ObjectiveVector compute_objective(
    const std::vector<ContainerLoad>& containers) noexcept;

/// 计算将箱子放入某容器后的增量目标变化
/// 用于搜索过程中的局部比较
[[nodiscard]] ObjectiveVector project_objective(
    const ObjectiveVector& current,
    const ContainerLoad& target_container,
    bool is_new_container,
    bool is_new_platform,
    const std::string& box_group,
    bool group_touches_new_container) noexcept;

// =============================================================
// 字典序比较辅助
// =============================================================

/// 比较两个目标向量，按 keys 指定维度和顺序进行字典序比较
/// 返回 -1 若 a < b（a 更优），0 若相等，1 若 a > b（b 更优）
[[nodiscard]] int compare_objectives(const ObjectiveVector& a,
                                     const ObjectiveVector& b,
                                     const std::vector<std::string>& keys) noexcept;

} // namespace hypercube
