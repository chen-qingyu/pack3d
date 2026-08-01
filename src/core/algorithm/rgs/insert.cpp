#include "insert.hpp"

#include <algorithm>
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

std::vector<Position> projection(
    const Position& p,
    int d,
    const ContainerLoad& load,
    const ContainerType& ctype) noexcept
{
    std::vector<Position> result;
    int theta_dim = (d == 0) ? 1 : 0;
    int eta_dim = (d == 2) ? 1 : 2;
    int32_t p_d = get_coord(p, d);
    int32_t p_theta = get_coord(p, theta_dim);
    int32_t p_eta = get_coord(p, eta_dim);

    struct Candidate
    {
        size_t idx;
        int32_t far_end;
    };
    std::vector<Candidate> cands;
    for (size_t i = 0; i < load.placements.size(); ++i)
    {
        const auto& pl = load.placements[i];
        int32_t c_d = get_coord(pl.position, d);
        int32_t s_d = get_size(pl.osize, d);

        if (c_d >= p_d)
        {
            continue;
        }

        int32_t far = c_d + s_d;
        if (far > p_d)
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

        cands.push_back({i, far});
    }
    std::sort(cands.begin(), cands.end(),
              [](const Candidate& a, const Candidate& b)
              { return a.far_end > b.far_end; });

    std::set<size_t> blocking;

    for (const auto& cand : cands)
    {
        const auto& pl = load.placements[cand.idx];

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

        blocking.insert(cand.idx);
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
    const ContainerType& ctype) noexcept
{
    std::vector<Position> new_eps;

    // Alg3: 对每个投影方向 j
    for (int j = 0; j < 3; ++j)
    {
        for (int d2 = 0; d2 < 3; ++d2)
        {
            if (d2 == j)
            {
                continue;
            }

            Position p = pos;
            set_coord(p, j, get_coord(p, j) + get_size(osize, j));
            set_coord(p, d2, get_coord(p, d2) + get_size(osize, d2));

            auto proj_eps = projection(p, d2, load, ctype);
            for (auto& ep : proj_eps)
            {
                new_eps.push_back(ep);
            }
        }
    }

    // Alg3 line 15: top-center point（能否堆叠由承重/支撑约束在 can_place 判定）
    new_eps.push_back({pos.x, pos.y, pos.z + osize.dz});

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

    if (!check_support(ep, osize, load, problem.support_rate))
    {
        return false;
    }

    if (problem.has_max_stack || problem.has_max_load)
    {
        if (!check_stack_constraints(ep, osize, box.weight.value_or(0.0), load, box_type_map))
        {
            return false;
        }
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

    if (problem.route.has_value())
    {
        if (!check_route_order(load, box.platform, ep, osize, problem.route.value()))
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
    pl.group = box.group;
    pl.weight = box.weight;

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
    if (!box.group.empty())
    {
        load.groups.insert(box.group);
    }

    grid_register(ctx, load.placements, idx);

    auto new_eps = gen_new_ep(ep, osize, load, *load.type);
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

std::vector<std::string> insertion_heuristic(
    const std::vector<Box>& items,
    const ContainerType& ctype,
    const std::map<std::string, BoxType>& box_type_map,
    SortCriterion criterion,
    double rho,
    const Problem& problem,
    ContainerLoad& out_load,
    EpContext& out_ctx) noexcept
{
    out_load.type = &ctype;
    out_ctx.grid_cell_size = compute_cell_size(items, box_type_map);

    out_ctx.extreme_points.insert({0, 0, 0});

    auto ordered = build_ordered_list(items, box_type_map, criterion, rho);

    // 预建 box_id -> Box 映射，避免内层 O(N^2) 线性搜索
    std::unordered_map<std::string, const Box*> box_by_id;
    box_by_id.reserve(items.size());
    for (const auto& bx : items)
    {
        box_by_id[bx.id] = &bx;
    }

    std::set<std::string> loaded_ids;
    std::vector<std::string> loaded_order;

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

        bool placed = false;
        for (const auto& ep : out_ctx.extreme_points)
        {
            for (auto orient : entry.orients)
            {
                if (can_place(box, bt, orient, ep, out_load, out_ctx, box_type_map, problem))
                {
                    commit_placement(out_load, out_ctx, box, bt, orient, ep, problem);
                    loaded_ids.insert(box.id);
                    loaded_order.push_back(box.id);
                    placed = true;
                    if (!TimeChecker::check())
                    {
                        return loaded_order;
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

    return loaded_order;
}

} // namespace pack3d::rgs
