#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "../../types.hpp"

namespace hypercube::rgs
{

// 排序策略（论文 §4.2，5 种）
enum class SortCriterion : uint8_t
{
    StackabilityCumulatedVolume,
    StackabilityHighestVolume,
    CumulatedVolume,
    HighestVolume,
    Random,
};

// 排序后的条目：箱子 ID + 允许的朝向列表
struct OrderEntry
{
    std::string box_id;
    std::vector<Orientation> orients;
};

// BuildOrderedList（论文 §4.2）
// 返回排序后的 (box_id, orients) 列表
[[nodiscard]] std::vector<OrderEntry> build_ordered_list(
    const std::vector<Box>& remaining,
    const std::map<std::string, BoxType>& box_type_map,
    SortCriterion criterion,
    double rho) noexcept;

} // namespace hypercube::rgs
