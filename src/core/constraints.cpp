#include "constraints.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pack3d
{

bool type_stackable(const BoxType& bt) noexcept
{
    for (auto o : bt.allowed_orientations)
    {
        auto ms = bt.max_stack_for(o);
        if (ms.has_value() && ms.value() < 2)
        {
            continue;
        }
        auto ml = bt.max_load_for(o);
        if (ml.has_value() && ml.value() <= 0)
        {
            continue;
        }
        return true;
    }
    return false;
}

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

bool check_obstacle(const Position& pos, const OrientedSize& osize,
                    const std::vector<Obstacle>& obstacles) noexcept
{
    for (const auto& o : obstacles)
    {
        if ((o.x + o.dx <= pos.x) || (pos.x + osize.dx <= o.x) ||
            (o.y + o.dy <= pos.y) || (pos.y + osize.dy <= o.y) ||
            (o.z + o.dz <= pos.z) || (pos.z + osize.dz <= o.z))
        {
            continue;
        }
        return true;
    }
    return false;
}

std::array<Position, 8> obstacle_corners(const Obstacle& o) noexcept
{
    return {{
        {o.x, o.y, o.z},
        {o.x + o.dx, o.y, o.z},
        {o.x, o.y + o.dy, o.z},
        {o.x + o.dx, o.y + o.dy, o.z},
        {o.x, o.y, o.z + o.dz},
        {o.x + o.dx, o.y, o.z + o.dz},
        {o.x, o.y + o.dy, o.z + o.dz},
        {o.x + o.dx, o.y + o.dy, o.z + o.dz},
    }};
}

bool check_facet(const Position& pos, const OrientedSize& osize,
                 const Size& csize, const std::vector<Facet>& facets) noexcept
{
    const int32_t extents[3] = {csize.x, csize.y, csize.z};
    for (const auto& f : facets)
    {
        const auto nf = normalize_facet(f);
        if (nf.u_axis < 0 || nf.v_axis < 0)
        {
            continue; // 防御：预校验保证恰好两个非零截距
        }
        // 极角点：最靠禁区的角；距禁区侧向内的距离
        auto corner_dist = [&](int axis, int64_t intercept) -> int64_t
        {
            int32_t lo = (axis == 0) ? pos.x : ((axis == 1) ? pos.y : pos.z);
            int32_t hi = lo + ((axis == 0) ? osize.dx : ((axis == 1) ? osize.dy : osize.dz));
            return (intercept > 0) ? (static_cast<int64_t>(extents[axis]) - hi)
                                   : static_cast<int64_t>(lo);
        };
        const int64_t d0 = corner_dist(nf.u_axis, nf.su);
        const int64_t d1 = corner_dist(nf.v_axis, nf.sv);
        const int64_t m0 = (nf.su < 0) ? -nf.su : nf.su;
        const int64_t m1 = (nf.sv < 0) ? -nf.sv : nf.sv;
        // 极角点落入楔形禁区 → 相交
        if (d0 * m1 + d1 * m0 < m0 * m1)
        {
            return true;
        }
    }
    return false;
}

bool facet_covers_origin(const Facet& f) noexcept
{
    int nonzero = 0;
    int neg = 0;
    for (int v : {f.dx, f.dy, f.dz})
    {
        if (v != 0)
        {
            ++nonzero;
            neg += (v < 0) ? 1 : 0;
        }
    }
    return nonzero == 2 && neg == 2;
}

bool facet_covers_origin(const std::vector<Facet>& facets) noexcept
{
    for (const auto& f : facets)
    {
        if (facet_covers_origin(f))
        {
            return true;
        }
    }
    return false;
}

std::optional<Position> first_floor_spot(const Size& csize,
                                         const std::vector<Facet>& facets,
                                         const OrientedSize& ref) noexcept
{
    const int sx = std::max(1, ref.dx);
    const int sy = std::max(1, ref.dy);
    for (int py = 0; py + ref.dy <= csize.y; py += sy)
    {
        for (int px = 0; px + ref.dx <= csize.x; px += sx)
        {
            const Position spot{px, py, 0};
            if (!check_facet(spot, ref, csize, facets))
            {
                return spot;
            }
        }
    }
    return std::nullopt;
}

std::vector<FacetSlab> facet_staircase(const Facet& f, const Size& csize, int steps) noexcept
{
    std::vector<FacetSlab> out;
    const auto nf = normalize_facet(f);
    if (nf.u_axis < 0 || nf.v_axis < 0 || steps <= 0)
    {
        return out;
    }
    const int32_t extents[3] = {csize.x, csize.y, csize.z};
    const int64_t mu = (nf.su < 0) ? -nf.su : nf.su;
    const int64_t mv = (nf.sv < 0) ? -nf.sv : nf.sv;

    auto set_axis = [&](int axis, int64_t lo, int64_t hi)
    {
        switch (axis)
        {
            case 0:
                out.back().x = static_cast<int32_t>(lo);
                out.back().dx = static_cast<int32_t>(hi - lo);
                break;
            case 1:
                out.back().y = static_cast<int32_t>(lo);
                out.back().dy = static_cast<int32_t>(hi - lo);
                break;
            default:
                out.back().z = static_cast<int32_t>(lo);
                out.back().dz = static_cast<int32_t>(hi - lo);
                break;
        }
    };

    for (int i = 1; i <= steps; ++i)
    {
        out.emplace_back();
        // u 方向：本片最大进深（角侧向内）
        int64_t u_depth = mu * (steps - i + 1) / steps;
        if (nf.su > 0)
        {
            set_axis(nf.u_axis, extents[nf.u_axis] - u_depth, extents[nf.u_axis]);
        }
        else
        {
            set_axis(nf.u_axis, 0, u_depth);
        }
        // v 方向：距角侧 [(i-1)*mv/steps, i*mv/steps]
        int64_t v_lo = (i - 1) * mv / steps;
        int64_t v_hi = i * mv / steps;
        if (nf.sv > 0)
        {
            set_axis(nf.v_axis, extents[nf.v_axis] - v_hi, extents[nf.v_axis] - v_lo);
        }
        else
        {
            set_axis(nf.v_axis, v_lo, v_hi);
        }
        // 平行轴贯穿全容器
        set_axis(nf.w_axis, 0, extents[nf.w_axis]);
    }
    return out;
}

bool check_weight(const ContainerLoad& load,
                  double box_weight) noexcept
{
    if (!load.type->payload.has_value())
    {
        return true;
    }
    return load.total_weight + box_weight <= load.type->payload.value() + 1e-9;
}

bool check_support(const Position& pos, const OrientedSize& osize,
                   const ContainerLoad& load,
                   double support_rate,
                   const std::vector<size_t>* indices) noexcept
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
    bool directly_supported = false;

    // indices 非空时只遍历网格支撑带候选（超集），过滤条件不变，结果与全量扫描一致
    const size_t n = indices ? indices->size() : load.placements.size();
    for (size_t k = 0; k < n; ++k)
    {
        const size_t idx = indices ? (*indices)[k] : k;
        if (idx >= load.placements.size())
        {
            continue;
        }
        const auto& pl = load.placements[idx];
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
        supported_area += static_cast<int64_t>(ox2 - ox1) * (oy2 - oy1);
    }

    // 障碍物顶面等价地板：顶面 z == 候选底面 z 且投影相交的面积计入支撑
    if (load.type != nullptr)
    {
        for (const auto& o : load.type->obstacles)
        {
            if (o.z + o.dz != pos.z)
            {
                continue;
            }
            int32_t ox1 = std::max(bx1, o.x);
            int32_t ox2 = std::min(bx2, o.x + o.dx);
            int32_t oy1 = std::max(by1, o.y);
            int32_t oy2 = std::min(by2, o.y + o.dy);
            if (ox1 >= ox2 || oy1 >= oy2)
            {
                continue;
            }
            directly_supported = true;
            supported_area += static_cast<int64_t>(ox2 - ox1) * (oy2 - oy1);
        }
    }

    if (!directly_supported)
    {
        return false;
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
                             const std::vector<Placement>& placements,
                             const std::vector<size_t>* indices = nullptr) noexcept
{
    SupportInfo info;
    int32_t bx1 = pos.x;
    int32_t bx2 = pos.x + osize.dx;
    int32_t by1 = pos.y;
    int32_t by2 = pos.y + osize.dy;

    // indices 非空时只遍历网格支撑带候选（超集），过滤条件不变，结果与全量扫描一致
    const size_t n = indices ? indices->size() : placements.size();
    for (size_t k = 0; k < n; ++k)
    {
        const size_t i = indices ? (*indices)[k] : k;
        if (i >= placements.size())
        {
            continue;
        }
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

// 分摊份额：weight 按受支撑面积分给某支撑的份额（分母=受支撑面积，悬空不计）
double load_share(double weight, int64_t area, int64_t total_area) noexcept
{
    if (total_area <= 0)
    {
        return 0.0;
    }
    return weight * static_cast<double>(area) / static_cast<double>(total_area);
}

// 聚合支撑链：把各直接支撑的份额沿其传递支撑链累计到每个链上箱。
// out_boxes 去重（每箱一次，用于 col_height 新层 +1 与 max_stack 每柱检查），
// out_delta 与之对齐，为该箱所有经过路径的份额之和（max_load 整柱累计）。
// 链由每箱已维护的 supports 下标构成（apply/recompute 保证支撑箱状态先于本箱建立）。
void aggregate_chain(const std::vector<Placement>& placements,
                     const std::vector<size_t>& supports,
                     const std::vector<double>& shares,
                     std::vector<size_t>& out_boxes,
                     std::vector<double>& out_delta) noexcept
{
    out_boxes.clear();
    out_delta.clear();
    for (size_t i = 0; i < supports.size(); ++i)
    {
        std::vector<size_t> stack{supports[i]};
        while (!stack.empty())
        {
            const size_t x = stack.back();
            stack.pop_back();
            const auto it = std::find(out_boxes.begin(), out_boxes.end(), x);
            if (it == out_boxes.end())
            {
                out_boxes.push_back(x);
                out_delta.push_back(shares[i]);
            }
            else
            {
                out_delta[static_cast<size_t>(it - out_boxes.begin())] += shares[i];
            }
            for (const size_t s : placements[x].supports)
            {
                stack.push_back(s);
            }
        }
    }
}

// 只读预检：A3 面积分摊（直接支撑逐对） + 沿支撑链 max_stack 每柱层数 / max_load 整柱累计。
// 无支撑（地板/障碍物顶）直接通过（开新柱、不受承重约束）。
// bottom_z = 候选箱底面 z（与柱顶 z 比较判断是否为新层，同层并排不增层）。
bool check_stack_chain(const ContainerLoad& load, const SupportInfo& info, double weight,
                       int32_t bottom_z,
                       const std::map<std::string, BoxType>& box_type_map) noexcept
{
    if (info.supports.empty())
    {
        return true;
    }
    std::vector<double> shares(info.supports.size());
    for (size_t i = 0; i < info.supports.size(); ++i)
    {
        shares[i] = load_share(weight, info.areas[i], info.total_area);
        const auto& S = load.placements[info.supports[i]];
        const auto& bt = box_type_map.at(S.box_type_id);
        // A3：分摊份额 <= 支撑箱按重叠面积比例分配到的容量
        const auto ml = bt.max_load_for(S.orientation);
        if (ml.has_value())
        {
            const double footprint = static_cast<double>(S.osize.dx) * S.osize.dy;
            const double alloc = ml.value() * static_cast<double>(info.areas[i]) / footprint;
            if (shares[i] > alloc + 1e-9)
            {
                return false;
            }
        }
    }
    std::vector<size_t> boxes;
    std::vector<double> delta;
    aggregate_chain(load.placements, info.supports, shares, boxes, delta);
    for (size_t k = 0; k < boxes.size(); ++k)
    {
        const auto& X = load.placements[boxes[k]];
        const auto& bt = box_type_map.at(X.box_type_id);
        // max_stack 每柱层数：仅当本箱位于柱顶（底面 == 柱顶 z）时柱 +1 并检查；同层并排不增层
        const auto ms = bt.max_stack_for(X.orientation);
        if (ms.has_value() && bottom_z == X.col_top_z && X.col_height + 1 > ms.value())
        {
            return false;
        }
        // max_load 整柱累计：新箱各路径份额之和流经该箱
        const auto ml = bt.max_load_for(X.orientation);
        if (ml.has_value() && X.cum_load + delta[k] > ml.value() + 1e-9)
        {
            return false;
        }
    }
    return true;
}

} // namespace

bool check_stack_constraints(
    const Position& pos, const OrientedSize& osize, double weight,
    const ContainerLoad& load,
    const std::map<std::string, BoxType>& box_type_map,
    const std::vector<size_t>* indices) noexcept
{
    const SupportInfo info = collect_supports(pos, osize, load.placements, indices);
    return check_stack_chain(load, info, weight, pos.z, box_type_map);
}

void apply_stack_state(const Position& pos, const OrientedSize& osize, double weight,
                       ContainerLoad& load) noexcept
{
    if (load.placements.empty())
    {
        return;
    }
    Placement& pl = load.placements.back();
    const int32_t bottom_z = pos.z;
    const int32_t top_z = pos.z + osize.dz;

    // 新箱顶面在 pos.z + dz，不会被 collect_supports 识别为支撑，可安全全量扫描
    const SupportInfo info = collect_supports(pos, osize, load.placements);
    pl.stack_level = info.supports.empty() ? 1 : info.max_level + 1;
    pl.cum_load = 0.0;
    pl.supports = info.supports;
    pl.col_top_z = top_z;
    if (info.supports.empty())
    {
        pl.col_height = 1;
        return;
    }

    // 本箱柱层数 = max(直接支撑柱层数 + (本箱为柱顶新层 ? 1 : 0))；同层并排不增层
    int max_col = 1;
    for (size_t i = 0; i < info.supports.size(); ++i)
    {
        const auto& S = load.placements[info.supports[i]];
        const int base = S.col_height + (bottom_z == S.col_top_z ? 1 : 0);
        max_col = std::max(max_col, base);
    }
    pl.col_height = max_col;

    std::vector<double> shares(info.supports.size());
    for (size_t i = 0; i < info.supports.size(); ++i)
    {
        shares[i] = load_share(weight, info.areas[i], info.total_area);
    }
    std::vector<size_t> boxes;
    std::vector<double> delta;
    aggregate_chain(load.placements, info.supports, shares, boxes, delta);
    for (size_t k = 0; k < boxes.size(); ++k)
    {
        auto& X = load.placements[boxes[k]];
        if (bottom_z == X.col_top_z)
        {
            X.col_height += 1;
            X.col_top_z = top_z;
        }
        X.cum_load += delta[k];
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
        pl.col_height = 1;
        pl.col_top_z = 0;
        pl.cum_load = 0.0;
        pl.supports.clear();
    }

    for (size_t k = 0; k < n; ++k)
    {
        auto& pl = load.placements[order[k]];
        const double weight = pl.weight.value_or(0.0);
        const int32_t bottom_z = pl.position.z;
        const int32_t top_z = pl.position.z + pl.osize.dz;

        // 直接支撑箱顶面 == 本箱底面 z，其 z 严格更小，z 序中必已处理
        const SupportInfo info = collect_supports(pl.position, pl.osize, load.placements);
        pl.supports = info.supports;
        pl.stack_level = info.supports.empty() ? 1 : info.max_level + 1;
        pl.col_top_z = top_z;
        if (info.supports.empty())
        {
            pl.col_height = 1;
            continue;
        }

        // 本箱柱层数 = max(直接支撑柱层数 + (本箱为柱顶新层 ? 1 : 0))
        int max_col = 1;
        for (size_t i = 0; i < info.supports.size(); ++i)
        {
            const auto& S = load.placements[info.supports[i]];
            const int base = S.col_height + (bottom_z == S.col_top_z ? 1 : 0);
            max_col = std::max(max_col, base);
        }
        pl.col_height = max_col;

        std::vector<double> shares(info.supports.size());
        for (size_t i = 0; i < info.supports.size(); ++i)
        {
            shares[i] = load_share(weight, info.areas[i], info.total_area);
        }
        std::vector<size_t> boxes;
        std::vector<double> delta;
        aggregate_chain(load.placements, info.supports, shares, boxes, delta);
        for (size_t j = 0; j < boxes.size(); ++j)
        {
            auto& X = load.placements[boxes[j]];
            if (bottom_z == X.col_top_z)
            {
                X.col_height += 1;
                X.col_top_z = top_z;
            }
            X.cum_load += delta[j];
            if (errors)
            {
                const auto& bt = box_type_map.at(X.box_type_id);
                const auto ms = bt.max_stack_for(X.orientation);
                if (ms.has_value() && X.col_height > ms.value())
                {
                    errors->push_back("stack " + std::to_string(X.col_height) +
                                      " > max_stack " + std::to_string(ms.value()) +
                                      " for box " + pl.box_id);
                }
                const auto ml = bt.max_load_for(X.orientation);
                if (ml.has_value() && X.cum_load > ml.value() + 1e-9)
                {
                    errors->push_back("load " + std::to_string(X.cum_load) +
                                      " > max_load " + std::to_string(ml.value()) +
                                      " for box " + pl.box_id);
                }
            }
        }
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
                       const RouteOrder& route,
                       const std::vector<size_t>* indices) noexcept
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

    // indices 非空时只遍历网格 YZ/XY 投影重叠候选（超集），规则只对这些箱生效，
    // 结果与全量扫描一致
    const size_t n = indices ? indices->size() : load.placements.size();
    for (size_t k = 0; k < n; ++k)
    {
        const size_t idx = indices ? (*indices)[k] : k;
        if (idx >= load.placements.size())
        {
            continue;
        }
        const auto& pl = load.placements[idx];
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

namespace
{

// DSU：按共享 group 合并 containers 中的容器，返回各容器的父指针（调用方需 find 取根）
std::vector<int> tender_roots(const std::vector<ContainerLoad>& all_loads,
                              const std::vector<int>& containers)
{
    std::vector<int> parent(all_loads.size());
    std::vector<int> sz(all_loads.size(), 1);
    for (size_t i = 0; i < parent.size(); ++i)
    {
        parent[i] = static_cast<int>(i);
    }
    auto find = [&parent](int x)
    {
        while (parent[x] != x)
        {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    auto unite = [&](int a, int b)
    {
        a = find(a);
        b = find(b);
        if (a == b)
        {
            return;
        }
        if (sz[a] < sz[b])
        {
            std::swap(a, b);
        }
        parent[b] = a;
        sz[a] += sz[b];
    };

    std::map<std::string, int> first_of_group;
    for (int i : containers)
    {
        for (const auto& g : all_loads[i].groups)
        {
            auto [it, inserted] = first_of_group.emplace(g, i);
            if (!inserted)
            {
                unite(it->second, i);
            }
        }
    }
    return parent;
}

} // namespace

TenderState build_tender_state(const std::vector<ContainerLoad>& all_loads,
                               size_t current, int tender_limit)
{
    TenderState ts;
    ts.limit = tender_limit;
    if (tender_limit <= 0)
    {
        return ts;
    }

    std::vector<int> committed;
    committed.reserve(all_loads.size() - (current < all_loads.size() ? 1 : 0));
    for (size_t i = 0; i < all_loads.size(); ++i)
    {
        if (i != current)
        {
            committed.push_back(static_cast<int>(i));
        }
    }

    auto parent = tender_roots(all_loads, committed);
    auto find = [&parent](int x)
    {
        while (parent[x] != x)
        {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };

    std::vector<int> comp_size(all_loads.size(), 0);
    for (int i : committed)
    {
        comp_size[find(i)]++;
    }

    std::map<int, int> root_to_id;
    for (int i : committed)
    {
        int root = find(i);
        auto [it, inserted] = root_to_id.emplace(root, static_cast<int>(ts.sizes.size()));
        if (inserted)
        {
            ts.sizes.push_back(comp_size[root]);
        }
        for (const auto& g : all_loads[i].groups)
        {
            auto& list = ts.group_tenders[g];
            if (list.empty() || list.back() != it->second)
            {
                list.push_back(it->second);
            }
        }
    }
    return ts;
}

bool check_tender_limit(const TenderState& ts,
                        const std::set<std::string>& groups,
                        const std::string& group) noexcept
{
    if (ts.limit <= 0 || group.empty() || groups.count(group))
    {
        return true;
    }
    std::vector<char> seen(ts.sizes.size(), 0);
    int merged = 1; // 当前容器自身
    auto absorb = [&](const std::vector<int>& tenders) noexcept
    {
        for (int t : tenders)
        {
            if (!seen[t])
            {
                seen[t] = 1;
                merged += ts.sizes[t];
            }
        }
    };
    for (const auto& g : groups)
    {
        auto it = ts.group_tenders.find(g);
        if (it != ts.group_tenders.end())
        {
            absorb(it->second);
        }
    }
    auto it = ts.group_tenders.find(group);
    if (it != ts.group_tenders.end())
    {
        absorb(it->second);
    }
    return merged <= ts.limit;
}

bool check_all_tenders(const std::vector<ContainerLoad>& all_loads, int tender_limit) noexcept
{
    if (tender_limit <= 0)
    {
        return true;
    }
    std::vector<int> all_idx(all_loads.size());
    for (size_t i = 0; i < all_loads.size(); ++i)
    {
        all_idx[i] = static_cast<int>(i);
    }
    auto parent = tender_roots(all_loads, all_idx);
    auto find = [&parent](int x)
    {
        while (parent[x] != x)
        {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    std::vector<int> comp_size(all_loads.size(), 0);
    for (size_t i = 0; i < all_loads.size(); ++i)
    {
        comp_size[find(static_cast<int>(i))]++;
    }
    for (int s : comp_size)
    {
        if (s > tender_limit)
        {
            return false;
        }
    }
    return true;
}

std::vector<std::optional<int>> compute_container_tenders(
    const std::vector<ContainerLoad>& all_loads)
{
    std::vector<std::optional<int>> out(all_loads.size());
    std::vector<int> all_idx(all_loads.size());
    for (size_t i = 0; i < all_loads.size(); ++i)
    {
        all_idx[i] = static_cast<int>(i);
    }
    auto parent = tender_roots(all_loads, all_idx);
    auto find = [&parent](int x)
    {
        while (parent[x] != x)
        {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    std::map<int, int> root_to_id;
    int next = 1;
    for (size_t i = 0; i < all_loads.size(); ++i)
    {
        if (all_loads[i].groups.empty())
        {
            continue; // 无 group 的容器不属于任何 tender → null
        }
        int root = find(static_cast<int>(i));
        auto [it, inserted] = root_to_id.emplace(root, next);
        if (inserted)
        {
            ++next;
        }
        out[i] = it->second;
    }
    return out;
}

} // namespace pack3d
