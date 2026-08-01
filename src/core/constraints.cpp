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

namespace
{

// 直接支撑箱信息：底面贴合（顶面 == 候选底面 z）且投影相交（面积 > 0）
// supports 为 placement 下标，避免指针悬空
struct SupportInfo
{
    std::vector<size_t> supports;
    std::vector<int64_t> areas; // 与 supports 对齐的重叠面积
    int64_t total_area = 0;     // 所有直接支撑箱重叠面积之和（悬空部分不计）
    int max_level = 0;          // 直接支撑箱的最大 stack_level
};

SupportInfo collect_supports(const Position& pos, const OrientedSize& osize,
                             const std::vector<Placement>& placements) noexcept
{
    SupportInfo info;
    int32_t bx1 = pos.x;
    int32_t bx2 = pos.x + osize.dx;
    int32_t by1 = pos.y;
    int32_t by2 = pos.y + osize.dy;

    for (size_t i = 0; i < placements.size(); ++i)
    {
        const auto& pl = placements[i];
        if (pl.position.z + pl.osize.dz != pos.z)
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
        int64_t area = static_cast<int64_t>(ox2 - ox1) * (oy2 - oy1);
        info.supports.push_back(i);
        info.areas.push_back(area);
        info.total_area += area;
        if (pl.stack_level > info.max_level)
        {
            info.max_level = pl.stack_level;
        }
    }
    return info;
}

// 按面积加权分摊重量：increment = weight * area / total_supported_area
double load_increment(double weight, int64_t area, int64_t total_area) noexcept
{
    if (total_area <= 0)
    {
        return 0.0;
    }
    return weight * static_cast<double>(area) / static_cast<double>(total_area);
}

} // namespace

bool check_stack_constraints(
    const Position& pos, const OrientedSize& osize, double weight,
    const ContainerLoad& load,
    const std::map<std::string, BoxType>& box_type_map) noexcept
{
    SupportInfo info = collect_supports(pos, osize, load.placements);
    int level = info.supports.empty() ? 1 : info.max_level + 1;

    for (size_t i = 0; i < info.supports.size(); ++i)
    {
        const auto& S = load.placements[info.supports[i]];
        const auto& bt = box_type_map.at(S.box_type_id);

        // 堆码层数：新箱层号不得超过每个直接支撑箱在该朝向的 max_stack
        auto ms = bt.max_stack_for(S.orientation);
        if (ms.has_value() && level > ms.value())
        {
            return false;
        }

        // 单箱承重：支撑箱已承重 + 面积加权增量 <= max_load
        auto ml = bt.max_load_for(S.orientation);
        if (ml.has_value())
        {
            double inc = load_increment(weight, info.areas[i], info.total_area);
            if (S.supported_load + inc > ml.value() + 1e-9)
            {
                return false;
            }
        }
    }
    return true;
}

void apply_stack_state(const Position& pos, const OrientedSize& osize, double weight,
                       ContainerLoad& load) noexcept
{
    if (load.placements.empty())
    {
        return;
    }
    Placement& pl = load.placements.back();

    // 新箱顶面在 pos.z + dz，不会被 collect_supports 识别为支撑，可安全全量扫描
    SupportInfo info = collect_supports(pos, osize, load.placements);
    pl.stack_level = info.supports.empty() ? 1 : info.max_level + 1;
    pl.supported_load = 0.0;
    for (size_t i = 0; i < info.supports.size(); ++i)
    {
        load.placements[info.supports[i]].supported_load +=
            load_increment(weight, info.areas[i], info.total_area);
    }
}

void recompute_stack_state(ContainerLoad& load,
                           const std::map<std::string, BoxType>& box_type_map,
                           std::vector<std::string>* errors) noexcept
{
    const size_t n = load.placements.size();

    // z 升序排列的原始下标，保证支撑箱先于其上方箱处理
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i)
    {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b)
              {
        const auto& pa = load.placements[a];
        const auto& pb = load.placements[b];
        if (pa.position.z != pb.position.z)
        {
            return pa.position.z < pb.position.z;
        }
        if (pa.position.y != pb.position.y)
        {
            return pa.position.y < pb.position.y;
        }
        return pa.position.x < pb.position.x; });

    for (auto& pl : load.placements)
    {
        pl.stack_level = 1;
        pl.supported_load = 0.0;
    }

    for (size_t k = 0; k < n; ++k)
    {
        auto& pl = load.placements[order[k]];
        const double weight = pl.weight.value_or(0.0);

        // 直接支撑箱顶面 == 本箱底面 z，其 z 严格更小，z 序中必已处理
        SupportInfo info = collect_supports(pl.position, pl.osize, load.placements);
        int max_level = 0;
        for (size_t s = 0; s < info.supports.size(); ++s)
        {
            auto& S = load.placements[info.supports[s]];
            S.supported_load += load_increment(weight, info.areas[s], info.total_area);
            max_level = std::max(max_level, S.stack_level + 1);

            if (errors)
            {
                const auto& bt = box_type_map.at(S.box_type_id);

                auto ms = bt.max_stack_for(S.orientation);
                if (ms.has_value() && max_level > ms.value())
                {
                    errors->push_back("stack " + std::to_string(max_level) +
                                      " > max_stack " + std::to_string(ms.value()) +
                                      " for box " + pl.box_id);
                }

                auto ml = bt.max_load_for(S.orientation);
                if (ml.has_value() && S.supported_load > ml.value() + 1e-9)
                {
                    errors->push_back("load " + std::to_string(S.supported_load) +
                                      " > max_load " + std::to_string(ml.value()) +
                                      " for box " + S.box_id);
                }
            }
        }
        pl.stack_level = (max_level == 0) ? 1 : max_level;
    }
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
