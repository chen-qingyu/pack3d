#include "constraints.hpp"

#include <algorithm>
#include <cmath>

namespace pack3d
{

bool check_boundary(const ContainerType& ctype, const Position& pos,
                    const OrientedSize& osize) noexcept
{
    if (pos.x < 0 || pos.y < 0 || pos.z < 0)
    {
        return false;
    }
    if (pos.x + osize.dx > ctype.inner_size.x)
    {
        return false;
    }
    if (pos.y + osize.dy > ctype.inner_size.y)
    {
        return false;
    }
    if (pos.z + osize.dz > ctype.inner_size.z)
    {
        return false;
    }
    return true;
}

bool check_overlap(const Position& pos, const OrientedSize& osize,
                   const std::vector<Placement>& existing) noexcept
{
    for (const auto& pl : existing)
    {
        if ((pl.position.x + pl.osize.dx <= pos.x) ||
            (pos.x + osize.dx <= pl.position.x) ||
            (pl.position.y + pl.osize.dy <= pos.y) ||
            (pos.y + osize.dy <= pl.position.y) ||
            (pl.position.z + pl.osize.dz <= pos.z) ||
            (pos.z + osize.dz <= pl.position.z))
        {
            continue;
        }
        return true;
    }
    return false;
}

bool check_weight(const ContainerLoad& load,
                  double box_weight) noexcept
{
    if (!load.type->max_weight.has_value())
    {
        return true;
    }
    return load.total_weight + box_weight <= load.type->max_weight.value() + 1e-9;
}

bool check_support(const Position& pos, const OrientedSize& osize,
                   const ContainerLoad& load,
                   const std::map<std::string, BoxType>& box_type_map,
                   double support_rate) noexcept
{
    if (support_rate <= 0.0 || pos.z == 0)
    {
        return true;
    }

    int64_t total_area = static_cast<int64_t>(osize.dx) * osize.dy;
    if (total_area <= 0)
    {
        return false;
    }

    int32_t bx1 = pos.x;
    int32_t bx2 = pos.x + osize.dx;
    int32_t by1 = pos.y;
    int32_t by2 = pos.y + osize.dy;

    int64_t supported_area = 0;
    int corner_supported = 0;
    bool directly_supported = false;

    for (const auto& pl : load.placements)
    {
        auto& bt = box_type_map.at(pl.box_type_id);

        int32_t support_top = pl.position.z + pl.osize.dz;
        if (support_top != pos.z)
        {
            continue;
        }

        int32_t ox1 = std::max(bx1, pl.position.x);
        int32_t ox2 = std::min(bx2, pl.position.x + pl.osize.dx);
        int32_t oy1 = std::max(by1, pl.position.y);
        int32_t oy2 = std::min(by2, pl.position.y + pl.osize.dy);

        if (ox1 >= ox2 || oy1 >= oy2)
        {
            continue;
        }

        // 非可堆叠箱子上方不能放置
        if (!bt.stackable)
        {
            return false;
        }

        directly_supported = true;

        // 四角快速通道
        if (bx1 >= pl.position.x && bx1 < pl.position.x + pl.osize.dx)
        {
            if (by1 >= pl.position.y && by1 < pl.position.y + pl.osize.dy)
            {
                corner_supported |= 1;
            }
            if (by2 > pl.position.y && by2 <= pl.position.y + pl.osize.dy)
            {
                corner_supported |= 2;
            }
        }
        if (bx2 > pl.position.x && bx2 <= pl.position.x + pl.osize.dx)
        {
            if (by1 >= pl.position.y && by1 < pl.position.y + pl.osize.dy)
            {
                corner_supported |= 4;
            }
            if (by2 > pl.position.y && by2 <= pl.position.y + pl.osize.dy)
            {
                corner_supported |= 8;
            }
        }

        supported_area += static_cast<int64_t>(ox2 - ox1) * (oy2 - oy1);
    }

    if (!directly_supported)
    {
        return false;
    }
    if (corner_supported == 15)
    {
        return true; // all 4 corners supported — skip area ratio
    }

    double ratio = static_cast<double>(supported_area) / static_cast<double>(total_area);
    return ratio + 1e-9 >= support_rate;
}

bool check_platform_limit(const ContainerLoad& load,
                          const std::string& platform,
                          int platform_limit) noexcept
{
    if (platform_limit <= 0)
    {
        return true;
    }
    if (load.platforms.count(platform))
    {
        return true;
    }
    return static_cast<int>(load.platforms.size()) < platform_limit;
}

static bool yz_overlap(const Position& a, const OrientedSize& as,
                       const Position& b, const OrientedSize& bs) noexcept
{
    if (a.y >= b.y + bs.dy || b.y >= a.y + as.dy)
    {
        return false;
    }
    if (a.z >= b.z + bs.dz || b.z >= a.z + as.dz)
    {
        return false;
    }
    return true;
}

static bool xy_overlap(const Position& a, const OrientedSize& as,
                       const Position& b, const OrientedSize& bs) noexcept
{
    if (a.x >= b.x + bs.dx || b.x >= a.x + as.dx)
    {
        return false;
    }
    if (a.y >= b.y + bs.dy || b.y >= a.y + as.dy)
    {
        return false;
    }
    return true;
}

bool check_route_order(const ContainerLoad& load,
                       const std::string& platform,
                       const Position& pos, const OrientedSize& osize,
                       const RouteOrder& route) noexcept
{
    if (platform.empty())
    {
        return true;
    }

    auto it = route.index_of.find(platform);
    if (it == route.index_of.end())
    {
        return true;
    }

    size_t my_idx = it->second;

    for (const auto& pl : load.placements)
    {
        if (pl.platform == platform || pl.platform.empty())
        {
            continue;
        }
        auto oit = route.index_of.find(pl.platform);
        if (oit == route.index_of.end())
        {
            continue;
        }

        size_t other_idx = oit->second;

        // YZ 重叠 → 先卸平台必须在近门处（X 更大）
        if (yz_overlap(pos, osize, pl.position, pl.osize))
        {
            if (my_idx < other_idx)
            {
                // candidate 是先卸平台 → 必须在 X 更大侧（近门）
                if (pos.x < pl.position.x + pl.osize.dx)
                {
                    return false;
                }
            }
            else
            {
                // candidate 是后卸平台 → 必须在 X 更小侧（深处）
                if (pos.x + osize.dx > pl.position.x)
                {
                    return false;
                }
            }
        }

        // XY 重叠 → 后卸平台不能叠压在先卸平台上方
        if (xy_overlap(pos, osize, pl.position, pl.osize))
        {
            if (my_idx < other_idx)
            {
                // candidate 是先卸平台，已存在的箱子是后卸平台；后卸不能压在先卸上
                if (pl.position.z == pos.z + osize.dz)
                {
                    return false;
                }
            }
            else
            {
                // candidate 是后卸平台，已存在的箱子是先卸平台；后卸不能压在先卸上
                if (pos.z == pl.position.z + pl.osize.dz)
                {
                    return false;
                }
            }
        }
    }

    return true;
}

} // namespace pack3d
