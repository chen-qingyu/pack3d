#include "geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hypercube
{

// =============================================================
// orient_size
// =============================================================
OrientedSize orient_size(const Size& base, Orientation o) noexcept
{
    auto [x, y, z] = base;
    switch (o)
    {
        case Orientation::XYZ:
            return {x, y, z};
        case Orientation::XZY:
            return {x, z, y};
        case Orientation::YXZ:
            return {y, x, z};
        case Orientation::YZX:
            return {y, z, x};
        case Orientation::ZXY:
            return {z, x, y};
        case Orientation::ZYX:
            return {z, y, x};
    }
    return {x, y, z}; // unreachable
}

// =============================================================
// 边界检查
// =============================================================
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

// =============================================================
// 重叠检查
// =============================================================
bool check_overlap(const Position& a_pos, const OrientedSize& a_size,
                   const Position& b_pos, const OrientedSize& b_size) noexcept
{
    // 分离轴——至少在一个轴上区间不相交
    if (a_pos.x + a_size.dx <= b_pos.x)
    {
        return false;
    }
    if (b_pos.x + b_size.dx <= a_pos.x)
    {
        return false;
    }
    if (a_pos.y + a_size.dy <= b_pos.y)
    {
        return false;
    }
    if (b_pos.y + b_size.dy <= a_pos.y)
    {
        return false;
    }
    if (a_pos.z + a_size.dz <= b_pos.z)
    {
        return false;
    }
    if (b_pos.z + b_size.dz <= a_pos.z)
    {
        return false;
    }
    return true; // 三个轴都重叠 -> 碰撞
}

bool check_overlap_any(const Position& pos, const OrientedSize& osize,
                       const std::vector<Placement>& existing,
                       const std::map<std::string, BoxType>& box_type_map) noexcept
{
    for (const auto& pl : existing)
    {
        auto* bt = resolve_box_type(pl.box_type_id, box_type_map);
        if (bt == nullptr)
        {
            continue;
        }
        auto e_size = orient_size(bt->size, pl.orientation);
        if (check_overlap(pl.position, e_size, pos, osize))
        {
            return true;
        }
    }
    return false;
}

// =============================================================
// 支撑率计算
// =============================================================
double calc_support_ratio(const Position& pos, const OrientedSize& osize,
                          const ContainerLoad& load,
                          const std::map<std::string, BoxType>& box_type_map) noexcept
{
    // 直接放在容器底板上，支撑率为 100%
    if (pos.y == 0)
    {
        return 1.0;
    }

    int64_t total_area = static_cast<int64_t>(osize.dx) * osize.dz;
    if (total_area <= 0)
    {
        return 0.0;
    }

    // 新箱子底面在 XZ 平面上的矩形
    int32_t bx1 = pos.x;
    int32_t bx2 = pos.x + osize.dx;
    int32_t bz1 = pos.z;
    int32_t bz2 = pos.z + osize.dz;

    int64_t supported_area = 0;

    for (const auto& pl : load.placements)
    {
        auto* bt = resolve_box_type(pl.box_type_id, box_type_map);
        if (bt == nullptr)
        {
            continue;
        }
        auto e_size = orient_size(bt->size, pl.orientation);

        // 支撑箱子的顶面 Y 范围：[pl.position.y + e_size.dy, ...]
        // 新箱子的底面在 pos.y，只有顶面恰好在 pos.y 的箱子才可能支撑
        int32_t support_top = pl.position.y + e_size.dy;
        if (support_top != pos.y)
        {
            continue;
        }

        // 在 XZ 平面上的交集矩形
        int32_t sx1 = std::max(bx1, pl.position.x);
        int32_t sx2 = std::min(bx2, pl.position.x + e_size.dx);
        int32_t sz1 = std::max(bz1, pl.position.z);
        int32_t sz2 = std::min(bz2, pl.position.z + e_size.dz);

        if (sx1 < sx2 && sz1 < sz2)
        {
            supported_area += static_cast<int64_t>(sx2 - sx1) * (sz2 - sz1);
        }
    }

    return static_cast<double>(supported_area) / static_cast<double>(total_area);
}

// =============================================================
// 极点生成
// =============================================================
std::vector<Position> generate_extreme_points(
    const Position& pos, const OrientedSize& osize,
    const ContainerLoad& load) noexcept
{
    std::vector<Position> eps;
    // 三个候选极点，来自箱子的三个正向面
    eps.push_back({pos.x + osize.dx, pos.y, pos.z}); // X 方向极点
    eps.push_back({pos.x, pos.y + osize.dy, pos.z}); // Y 方向极点
    eps.push_back({pos.x, pos.y, pos.z + osize.dz}); // Z 方向极点
    return eps;
}

// =============================================================
// 过滤极点
// =============================================================
void filter_extreme_points(std::vector<Position>& points,
                           const ContainerLoad& load,
                           const std::map<std::string, BoxType>& box_type_map) noexcept
{
    auto& ctype = load.type;
    if (ctype == nullptr)
    {
        points.clear();
        return;
    }

    std::vector<Position> filtered;
    filtered.reserve(points.size());

    for (const auto& pt : points)
    {
        // 必须在容器内部
        if (pt.x < 0 || pt.y < 0 || pt.z < 0)
        {
            continue;
        }
        if (pt.x > ctype->inner_size.x ||
            pt.y > ctype->inner_size.y ||
            pt.z > ctype->inner_size.z)
        {
            continue;
        }

        // 不能在已有箱子内部
        bool inside_existing = false;
        for (const auto& pl : load.placements)
        {
            auto* bt = resolve_box_type(pl.box_type_id, box_type_map);
            if (bt == nullptr)
            {
                continue;
            }
            auto e_size = orient_size(bt->size, pl.orientation);
            if (pt.x >= pl.position.x && pt.x < pl.position.x + e_size.dx &&
                pt.y >= pl.position.y && pt.y < pl.position.y + e_size.dy &&
                pt.z >= pl.position.z && pt.z < pl.position.z + e_size.dz)
            {
                inside_existing = true;
                break;
            }
        }
        if (!inside_existing)
        {
            filtered.push_back(pt);
        }
    }

    points = std::move(filtered);
}

// =============================================================
// 查找辅助
// =============================================================
const BoxType* resolve_box_type(const std::string& id,
                                const std::map<std::string, BoxType>& map) noexcept
{
    auto it = map.find(id);
    return it != map.end() ? &it->second : nullptr;
}

std::map<std::string, BoxType> build_box_type_map(const std::vector<BoxType>& types) noexcept
{
    std::map<std::string, BoxType> m;
    for (const auto& bt : types)
    {
        m[bt.id] = bt;
    }
    return m;
}

std::map<std::string, ContainerType> build_container_type_map(
    const std::vector<ContainerType>& types) noexcept
{
    std::map<std::string, ContainerType> m;
    for (const auto& ct : types)
    {
        m[ct.id] = ct;
    }
    return m;
}

} // namespace hypercube
