#pragma once

#include "../../types.hpp"

namespace pack3d::bsg
{

/// BSG 多容器调度层
/// 选择最大的容器类型，将所有箱子打包进去
Solution pack(const Problem& problem,
              const std::map<std::string, BoxType>& box_type_map,
              const std::map<std::string, Box>& box_map);

} // namespace pack3d::bsg
