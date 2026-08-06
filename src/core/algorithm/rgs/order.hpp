#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "../../types.hpp"

namespace pack3d::rgs
{

// 排序策略（论文 §4.2 的 5 种 + RouteOrder 扩展）
enum class SortCriterion : uint8_t
{
    StackabilityCumulatedVolume,
    StackabilityHighestVolume,
    CumulatedVolume,
    HighestVolume,
    Random,
    RouteOrder, // 深处平台（route 索引大，后卸）优先，让 X 小侧先占满
};

// 排序后的条目：箱子 ID + 允许的朝向列表
struct OrderEntry
{
    std::string box_id;
    std::vector<Orientation> orients;
};

// BuildOrderedList（论文 §4.2）
// 返回排序后的 (box_id, orients) 列表；RouteOrder 准则需要 route 计算平台深度
[[nodiscard]] std::vector<OrderEntry> build_ordered_list(
    const std::vector<Box>& remaining,
    const std::map<std::string, BoxType>& box_type_map,
    SortCriterion criterion,
    double rho,
    const std::optional<RouteOrder>& route = std::nullopt) noexcept;

} // namespace pack3d::rgs
