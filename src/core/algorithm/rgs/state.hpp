#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "../../types.hpp"

namespace pack3d::rgs
{

// EP 排序：z,y,x 升序
struct EpOrder
{
    bool operator()(const Position& a, const Position& b) const noexcept
    {
        if (a.z != b.z)
        {
            return a.z < b.z;
        }
        if (a.y != b.y)
        {
            return a.y < b.y;
        }
        return a.x < b.x;
    }
};

// RGS EP-first-fit 运行时状态
struct EpContext
{
    int32_t grid_cell_size = 1;
    int32_t grid_cx = 1; // 网格单元数（容器内尺寸 / cell_size，上取整），route 投影查询用
    int32_t grid_cy = 1;
    int32_t grid_cz = 1;
    std::map<std::tuple<int32_t, int32_t, int32_t>, std::vector<size_t>> grid;
    std::set<Position, EpOrder> extreme_points;
};

} // namespace pack3d::rgs
