#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "../../types.hpp"

namespace hypercube::rgs
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
    std::map<std::tuple<int32_t, int32_t, int32_t>, std::vector<size_t>> grid;
    std::set<Position, EpOrder> extreme_points;
};

} // namespace hypercube::rgs
