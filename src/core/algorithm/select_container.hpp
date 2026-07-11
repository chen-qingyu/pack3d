#pragma once

#include <map>
#include <string>
#include <vector>

#include "../types.hpp"

namespace pack3d
{

/// 统一选容器：体积降序，返回第一个能装下至少一个剩余箱子的
[[nodiscard]] const ContainerType* select_largest_fitting(
    const std::vector<ContainerType>& container_types,
    const std::map<std::string, int>& usage,
    const std::vector<Box>& remaining,
    const std::map<std::string, BoxType>& box_type_map) noexcept;

} // namespace pack3d
