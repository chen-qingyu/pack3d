#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "../../types.hpp"

namespace pack3d::rgs
{

// 排序策略（论文 §4.2 的 5 种）
// route 存在时 build_ordered_list 对任意准则的确定性 pass 追加深度优先稳定排序
// （深处平台先放）；Shaw 迭代保持准则原排序 + 硬门校验
enum class SortCriterion : uint8_t
{
    StackabilityCumulatedVolume,
    StackabilityHighestVolume,
    CumulatedVolume,
    HighestVolume,
    Random,
};

// 排序后的条目：箱子 ID + 允许的朝向列表（固定数组，避免逐箱堆分配）
struct OrderEntry
{
    std::string box_id;
    std::array<Orientation, 6> orients{}; // 至多 6 种朝向（Orientation 枚举值数）
    uint8_t orient_count = 0;
};

// BuildOrderedList（论文 §4.2）
// 返回排序后的 (box_id, orients) 列表；route 非空且 rho==0 时按平台深度优先稳定排序
[[nodiscard]] std::vector<OrderEntry> build_ordered_list(
    const std::vector<Box>& remaining,
    const std::map<std::string, BoxType>& box_type_map,
    SortCriterion criterion,
    double rho,
    const std::optional<RouteOrder>& route = std::nullopt) noexcept;

} // namespace pack3d::rgs
