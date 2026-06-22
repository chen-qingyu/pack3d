#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "../../types.hpp"

namespace hypercube::rgs
{

// 单 ULD 评分：体积率 + 难装件惩罚（论文式(4)）
[[nodiscard]] double score_uld(
    const ContainerLoad& load,
    const std::vector<std::string>& unloaded_box_ids,
    const std::map<std::string, Box>& box_map,
    const std::map<std::string, BoxType>& box_type_map,
    const std::vector<ContainerType>& container_types,
    double total_penalty_denom) noexcept;

// 预计算难装件惩罚的分母（所有箱子一次算好）
[[nodiscard]] double compute_penalty_denom(
    const std::vector<Box>& all_boxes,
    const std::map<std::string, BoxType>& box_type_map,
    const std::vector<ContainerType>& container_types) noexcept;

// 计算单个箱子的「能进多少种 ULD」
[[nodiscard]] int count_fit_uld_types(
    const Box& box,
    const BoxType& box_type,
    const std::vector<ContainerType>& container_types) noexcept;

} // namespace hypercube::rgs
