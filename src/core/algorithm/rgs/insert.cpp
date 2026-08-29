#include "insert.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <unordered_map>

#include "../../constraints.hpp"
#include "../../tool.hpp"
#include "grid.hpp"

namespace pack3d::rgs
{

namespace
{

bool blocks(const Placement& b, const Placement& l, int d) noexcept
{
    if (d == 0)
    {
        return b.position.y <= l.position.y &&
               b.position.y + b.osize.dy >= l.position.y + l.osize.dy &&
               b.position.z <= l.position.z &&
               b.position.z + b.osize.dz >= l.position.z + l.osize.dz;
    }
    if (d == 1)
    {
        return b.position.x <= l.position.x &&
               b.position.x + b.osize.dx >= l.position.x + l.osize.dx &&
               b.position.z <= l.position.z &&
               b.position.z + b.osize.dz >= l.position.z + l.osize.dz;
    }
    return b.position.x <= l.position.x &&
           b.position.x + b.osize.dx >= l.position.x + l.osize.dx &&
           b.position.y <= l.position.y &&
           b.position.y + b.osize.dy >= l.position.y + l.osize.dy;
}

int32_t get_coord(const Position& p, int d) noexcept
{
    if (d == 0)
    {
        return p.x;
    }
    if (d == 1)
    {
        return p.y;
    }
    return p.z;
}

int32_t get_size(const OrientedSize& os, int d) noexcept
{
    if (d == 0)
    {
        return os.dx;
    }
    if (d == 1)
    {
        return os.dy;
    }
    return os.dz;
}

void set_coord(Position& p, int d, int32_t v) noexcept
{
    if (d == 0)
    {
        p.x = v;
    }
    else if (d == 1)
    {
        p.y = v;
    }
    else
    {
        p.z = v;
    }
}

} // anonymous namespace

// 论文 Alg4：order 为 gen_new_ep 预排好的、按方向 d 端位置降序的 placement 下标序列
// （一次排序复用）；这里沿有序序列过滤 + 阻塞判定，避免每次投影重新排序
std::vector<Position> projection(
    const Position& p,
    int d,
    const ContainerLoad& load,
    const std::vector<size_t>& order) noexcept
{
    std::vector<Position> result;
    int theta_dim = (d == 0) ? 1 : 0;
    int eta_dim = (d == 2) ? 1 : 2;
    int32_t p_d = get_coord(p, d);
    int32_t p_theta = get_coord(p, theta_dim);
    int32_t p_eta = get_coord(p, eta_dim);

    std::set<size_t> blocking;

    for (size_t idx : order)
    {
        if (idx >= load.placements.size())
        {
            continue;
        }
        const auto& pl = load.placements[idx];

        // 过滤（与旧实现一致）：候选在投影方向上完全在起点之前，且非投影方向投影相交
        int32_t c_d = get_coord(pl.position, d);
        int32_t s_d = get_size(pl.osize, d);
        if (c_d >= p_d)
        {
            continue;
        }
        if (c_d + s_d > p_d)
        {
            continue;
        }

        int32_t c_theta = get_coord(pl.position, theta_dim);
        int32_t s_theta = get_size(pl.osize, theta_dim);
        if (c_theta + s_theta < p_theta)
        {
            continue;
        }

        int32_t c_eta = get_coord(pl.position, eta_dim);
        int32_t s_eta = get_size(pl.osize, eta_dim);
        if (c_eta + s_eta < p_eta)
        {
            continue;
        }

        bool blocked = false;
        for (size_t bi : blocking)
        {
            if (blocks(load.placements[bi], pl, d))
            {
                blocked = true;
                break;
            }
        }
        if (blocked)
        {
            continue;
        }

        Position e_hit = p;
        set_coord(e_hit, d, get_coord(pl.position, d) + get_size(pl.osize, d));
        result.push_back(e_hit);

        int32_t c_th = get_coord(pl.position, theta_dim);
        int32_t c_et = get_coord(pl.position, eta_dim);
        if (p_theta > c_th && p_eta > c_et)
        {
            return result;
        }

        blocking.insert(idx);
    }

    Position e_wall = p;
    set_coord(e_wall, d, 0);
    result.push_back(e_wall);
    return result;
}

std::vector<Position> gen_new_ep(
    const Position& pos,
    const OrientedSize& osize,
    const ContainerLoad& load,
    bool stackable) noexcept
{
    std::vector<Position> new_eps;

    // 论文 Alg4：每个投影方向对已放物品按端位置排序一次，6 个投影起点复用同一
    // 有序序列（排序 O(P log P) 从 6 次降到 3 次）；tie-break 按下标升序保证确定。
    std::array<std::vector<size_t>, 3> order_by_dir;
    for (int d = 0; d < 3; ++d)
    {
        std::vector<size_t>& order = order_by_dir[d];
        order.reserve(load.placements.size());
        for (size_t i = 0; i < load.placements.size(); ++i)
        {
            order.push_back(i);
        }
        std::sort(order.begin(), order.end(),
                  [d, &load](size_t a, size_t b)
                  {
                      const int32_t fa = get_coord(load.placements[a].position, d) +
                                         get_size(load.placements[a].osize, d);
                      const int32_t fb = get_coord(load.placements[b].position, d) +
                                         get_size(load.placements[b].osize, d);
                      if (fa != fb)
                      {
                          return fa > fb;
                      }
                      return a < b;
                  });
    }

    // Alg3: 对每个投影方向 j
    for (int j = 0; j < 3; ++j)
    {
        // 论文 Alg3：非可堆叠箱跳过从顶部角点出发的 x/y 投影（j==z 的两对），
        // 避免生成要求直接落在其顶面上的 EP
        if (!stackable && j == 2)
        {
            continue;
        }
        for (int d2 = 0; d2 < 3; ++d2)
        {
            if (d2 == j)
            {
                continue;
            }

            Position p = pos;
            set_coord(p, j, get_coord(p, j) + get_size(osize, j));
            set_coord(p, d2, get_coord(p, d2) + get_size(osize, d2));

            auto proj_eps = projection(p, d2, load, order_by_dir[d2]);
            for (auto& ep : proj_eps)
            {
                new_eps.push_back(ep);
            }
        }
    }

    // Alg3 line 15: 顶部中心 EP（论文仅对可堆叠箱添加；能否堆叠由 can_place 判定）
    if (stackable)
    {
        new_eps.push_back({pos.x, pos.y, pos.z + osize.dz});
    }

    return new_eps;
}

bool can_place(
    const Box& box,
    const BoxType& box_type,
    Orientation orient,
    const Position& ep,
    const ContainerLoad& load,
    const EpContext& ctx,
    const std::map<std::string, BoxType>& box_type_map,
    const Problem& problem) noexcept
{
    auto osize = box_type.size.orient(orient);

    if (!check_boundary(*load.type, ep, osize))
    {
        return false;
    }

    auto neighbors = grid_neighbors(ctx, load.placements, ep, osize);
    if (grid_collides(load.placements, ep, osize, neighbors))
    {
        return false;
    }

    if (check_obstacle(ep, osize, load.type->obstacles))
    {
        return false;
    }

    if (check_facet(ep, osize, load.type->inner_size, load.type->facets))
    {
        return false;
    }

    // 支撑/堆叠检查走网格（论文 §4.4.4）：只查候选底面正下方支撑带的已放箱，
    // 避免全量扫描。grid_support_neighbors 返回的是支撑箱超集，check_support /
    // check_stack_constraints 仍按顶面贴合 + 投影相交过滤，结果与全量扫描一致。
    const bool need_support_cands =
        (problem.support_rate > 0.0 && ep.z > 0) || problem.has_max_stack ||
        problem.has_max_load || problem.heavy_not_on_light;
    std::vector<size_t> support_cands;
    if (need_support_cands)
    {
        support_cands = grid_support_neighbors(ctx, ep, osize);
    }
    const std::vector<size_t>* sc = need_support_cands ? &support_cands : nullptr;

    if (problem.support_rate > 0.0 && !check_support(ep, osize, load, problem.support_rate, sc))
    {
        return false;
    }

    if ((problem.has_max_stack || problem.has_max_load) &&
        !check_stack_constraints(ep, osize, box.box_type_id, orient,
                                 box.weight.value_or(0.0), load, box_type_map, sc))
    {
        return false;
    }

    if (problem.heavy_not_on_light &&
        !check_heavy_not_on_light(ep, osize, box.weight.value_or(0.0), load, sc))
    {
        return false;
    }

    if (box.weight.has_value())
    {
        if (!check_weight(load, box.weight.value()))
        {
            return false;
        }
    }

    if (problem.platform_limit.has_value() && problem.platform_limit.value() > 0)
    {
        if (!check_platform_limit(load, box.platform, problem.platform_limit.value()))
        {
            return false;
        }
    }

    // 路线检查同样走网格：只查与候选 YZ 或 XY 投影重叠的已放箱（check_route_order
    // 的规则只对这些箱生效），避免全量扫描。
    if (problem.route.has_value() && !box.platform.empty() &&
        problem.route.value().index_of.count(box.platform) != 0)
    {
        std::vector<size_t> route_neighbors = grid_route_neighbors(ctx, ep, osize);
        if (!check_route_order(load, box.platform, ep, osize, problem.route.value(), &route_neighbors))
        {
            return false;
        }
    }

    return true;
}

void commit_placement(
    ContainerLoad& load,
    EpContext& ctx,
    const Box& box,
    const BoxType& box_type,
    Orientation orient,
    const Position& ep,
    const Problem& problem) noexcept
{
    auto osize = box_type.size.orient(orient);

    Placement pl;
    pl.box_id = box.id;
    pl.box_type_id = box.box_type_id;
    pl.container_id = load.instance_id;
    pl.position = ep;
    pl.orientation = orient;
    pl.osize = osize;
    pl.platform = box.platform;
    pl.groups = box.groups;
    pl.weight = box.weight;
    pl.danger = box.danger.value_or(false);

    size_t idx = load.placements.size();
    load.placements.push_back(std::move(pl));

    if (problem.has_max_stack || problem.has_max_load)
    {
        apply_stack_state(ep, osize, box.weight.value_or(0.0), load);
    }

    load.used_volume += osize.volume();
    if (box.weight.has_value())
    {
        load.total_weight += box.weight.value();
    }
    if (!box.platform.empty())
    {
        load.platforms.insert(box.platform);
    }
    for (const auto& group : load.placements.back().groups)
    {
        load.groups.insert(group);
    }

    grid_register(ctx, load.placements, idx);

    auto new_eps = gen_new_ep(ep, osize, load, type_stackable(box_type));
    for (auto& nep : new_eps)
    {
        if (nep.x < 0 || nep.y < 0 || nep.z < 0)
        {
            continue;
        }
        if (nep.x > load.inner_x() || nep.y > load.inner_y() || nep.z > load.inner_z())
        {
            continue;
        }
        ctx.extreme_points.insert(nep);
    }
}

void insertion_heuristic(
    const std::vector<Box>& items,
    const ContainerType& ctype,
    const std::map<std::string, BoxType>& box_type_map,
    SortCriterion criterion,
    double rho,
    const Problem& problem,
    ContainerLoad& out_load,
    EpContext& out_ctx,
    const TenderState& tender,
    int32_t cell_size) noexcept
{
    out_load.type = &ctype;
    out_ctx.grid_cell_size = cell_size;
    out_ctx.grid_cx = (ctype.inner_size.x + cell_size - 1) / cell_size;
    out_ctx.grid_cy = (ctype.inner_size.y + cell_size - 1) / cell_size;
    out_ctx.grid_cz = (ctype.inner_size.z + cell_size - 1) / cell_size;

    // 已有放置（resume/续塞/后处理 trial）注册进碰撞网格：grid_collides 依赖它
    // 检测与旧箱的重叠，否则会把新箱放到旧箱位置（此前会重叠放置）
    for (size_t i = 0; i < out_load.placements.size(); ++i)
    {
        grid_register(out_ctx, out_load.placements, i);
    }

    out_ctx.extreme_points.insert({0, 0, 0});
    // 斜面禁区覆盖原点时原点不可用：用最大箱首个朝向扫描地板找可用起点
    if (facet_covers_origin(ctype.facets) && !items.empty())
    {
        const Box* ref = nullptr;
        int64_t best_vol = 0;
        for (const auto& bx : items)
        {
            auto it = box_type_map.find(bx.box_type_id);
            if (it == box_type_map.end())
            {
                continue;
            }
            const int64_t v = it->second.size.volume();
            if (ref == nullptr || v > best_vol)
            {
                ref = &bx;
                best_vol = v;
            }
        }
        if (ref != nullptr)
        {
            const auto& bt = box_type_map.at(ref->box_type_id);
            auto spot = first_floor_spot(ctype.inner_size, ctype.facets,
                                         bt.size.orient(bt.allowed_orientations[0]));
            if (spot.has_value())
            {
                out_ctx.extreme_points.insert(*spot);
            }
        }
    }

    // 障碍物 8 角点（4 顶角可上到顶面，4 底角可贴侧放置）
    for (const auto& o : ctype.obstacles)
    {
        for (const auto& c : obstacle_corners(o))
        {
            out_ctx.extreme_points.insert(c);
        }
    }

    auto ordered = build_ordered_list(items, box_type_map, criterion, rho, problem.route);

    // 预建 box_id -> Box 映射，避免内层 O(N^2) 线性搜索
    std::unordered_map<std::string, const Box*> box_by_id;
    box_by_id.reserve(items.size());
    for (const auto& bx : items)
    {
        box_by_id[bx.id] = &bx;
    }

    std::set<std::string> loaded_ids;

    for (const auto& entry : ordered)
    {
        if (loaded_ids.count(entry.box_id))
        {
            continue;
        }

        auto bx_it = box_by_id.find(entry.box_id);
        if (bx_it == box_by_id.end())
        {
            continue;
        }
        const Box& box = *bx_it->second;
        auto bt_it = box_type_map.find(box.box_type_id);
        if (bt_it == box_type_map.end())
        {
            continue;
        }
        const BoxType& bt = bt_it->second;

        // tender 约束：与位置无关，先于极点循环判断
        if (!check_tender_limit(tender, out_load.groups, box.groups))
        {
            continue; // 放入本容器会使所属 tender 超限 → 留未装
        }

        bool placed = false;
        for (const auto& ep : out_ctx.extreme_points)
        {
            for (uint8_t k = 0; k < entry.orient_count; ++k)
            {
                const Orientation orient = entry.orients[k];
                if (can_place(box, bt, orient, ep, out_load, out_ctx, box_type_map, problem))
                {
                    commit_placement(out_load, out_ctx, box, bt, orient, ep, problem);
                    loaded_ids.insert(box.id);
                    placed = true;
                    if (!TimeChecker::check())
                    {
                        return;
                    }
                    break;
                }
            }
            if (placed)
            {
                break;
            }
        }
    }
}

} // namespace pack3d::rgs
