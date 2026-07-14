#include "space.hpp"

#include <algorithm>
#include <limits>

namespace pack3d::bsg
{

SpaceSelection select_free_space(const std::vector<Cuboid>& spaces,
                                 const Size& container_size) noexcept
{
    SpaceSelection best;
    best.distance = std::numeric_limits<int32_t>::max();

    for (size_t i = 0; i < spaces.size(); ++i)
    {
        const auto& r = spaces[i];
        for (int x_side = 0; x_side < 2; ++x_side)
        {
            for (int y_side = 0; y_side < 2; ++y_side)
            {
                for (int z_side = 0; z_side < 2; ++z_side)
                {
                    Position anchor{
                        x_side == 0 ? r.pos.x : r.x_max(),
                        y_side == 0 ? r.pos.y : r.y_max(),
                        z_side == 0 ? r.pos.z : r.z_max(),
                    };
                    int32_t distance =
                        (x_side == 0 ? anchor.x : container_size.x - anchor.x) +
                        (y_side == 0 ? anchor.y : container_size.y - anchor.y) +
                        (z_side == 0 ? anchor.z : container_size.z - anchor.z);
                    if (distance < best.distance ||
                        (distance == best.distance && r.volume() > spaces[best.cuboid_index].volume()))
                    {
                        best = {i, anchor, distance};
                    }
                }
            }
        }
    }

    return best;
}

Position placement_position(const Cuboid& r, const GeneralBlock& b,
                            const Position& anchor) noexcept
{
    return {
        anchor.x == r.pos.x ? anchor.x : anchor.x - b.osize.dx,
        anchor.y == r.pos.y ? anchor.y : anchor.y - b.osize.dy,
        anchor.z == r.pos.z ? anchor.z : anchor.z - b.osize.dz,
    };
}

namespace
{

// 从 cuboid r 中挖掉区域 [ox_min, ox_max) × [oy_min, oy_max) × [oz_min, oz_max)
// 最多产生 6 个可重叠 cuboid，构成 residual-space cover。
void subtract_overlap(const Cuboid& r,
                      int32_t ox_min, int32_t ox_max,
                      int32_t oy_min, int32_t oy_max,
                      int32_t oz_min, int32_t oz_max,
                      std::vector<Cuboid>& out)
{
    int32_t rx_min = r.pos.x;
    int32_t rx_max = r.x_max();
    int32_t ry_min = r.pos.y;
    int32_t ry_max = r.y_max();
    int32_t rz_min = r.pos.z;
    int32_t rz_max = r.z_max();

    // 实际重叠区域（夹到 r 内）
    int32_t cx_min = std::max(rx_min, ox_min);
    int32_t cx_max = std::min(rx_max, ox_max);
    int32_t cy_min = std::max(ry_min, oy_min);
    int32_t cy_max = std::min(ry_max, oy_max);
    int32_t cz_min = std::max(rz_min, oz_min);
    int32_t cz_max = std::min(rz_max, oz_max);

    if (cx_min >= cx_max || cy_min >= cy_max || cz_min >= cz_max)
    {
        // 无重叠，保留原 cuboid
        out.push_back(r);
        return;
    }

    // 6 个方向的剩余 cuboid（每个维度 > 0 才保留）

    // X+ (right of overlap)
    if (cx_max < rx_max)
    {
        Cuboid c;
        c.pos = {cx_max, ry_min, rz_min};
        c.lx = rx_max - cx_max;
        c.ly = r.ly;
        c.lz = r.lz;
        out.push_back(c);
    }

    // X- (left of overlap)
    if (cx_min > rx_min)
    {
        Cuboid c;
        c.pos = {rx_min, ry_min, rz_min};
        c.lx = cx_min - rx_min;
        c.ly = r.ly;
        c.lz = r.lz;
        out.push_back(c);
    }

    // Y+ (front of overlap)
    if (cy_max < ry_max)
    {
        Cuboid c;
        c.pos = {rx_min, cy_max, rz_min};
        c.lx = r.lx;
        c.ly = ry_max - cy_max;
        c.lz = r.lz;
        out.push_back(c);
    }

    // Y- (back of overlap)
    if (cy_min > ry_min)
    {
        Cuboid c;
        c.pos = {rx_min, ry_min, rz_min};
        c.lx = r.lx;
        c.ly = cy_min - ry_min;
        c.lz = r.lz;
        out.push_back(c);
    }

    // Z+ (top of overlap)
    if (cz_max < rz_max)
    {
        Cuboid c;
        c.pos = {rx_min, ry_min, cz_max};
        c.lx = r.lx;
        c.ly = r.ly;
        c.lz = rz_max - cz_max;
        out.push_back(c);
    }

    // Z- (bottom of overlap)
    if (cz_min > rz_min)
    {
        Cuboid c;
        c.pos = {rx_min, ry_min, rz_min};
        c.lx = r.lx;
        c.ly = r.ly;
        c.lz = cz_min - rz_min;
        out.push_back(c);
    }
}

} // namespace

void update_residual_space(
    std::vector<Cuboid>& R,
    const Position& block_pos,
    const OrientedSize& block_size)
{
    int32_t bx_min = block_pos.x;
    int32_t bx_max = block_pos.x + block_size.dx;
    int32_t by_min = block_pos.y;
    int32_t by_max = block_pos.y + block_size.dy;
    int32_t bz_min = block_pos.z;
    int32_t bz_max = block_pos.z + block_size.dz;

    // 构建 block cuboid（用于重叠检测）
    Cuboid block_cuboid;
    block_cuboid.pos = block_pos;
    block_cuboid.lx = block_size.dx;
    block_cuboid.ly = block_size.dy;
    block_cuboid.lz = block_size.dz;

    std::vector<Cuboid> new_R;

    for (const auto& r : R)
    {
        if (r.overlaps(block_cuboid))
        {
            // 从 r 中挖掉 block 占用的区域
            subtract_overlap(r, bx_min, bx_max, by_min, by_max, bz_min, bz_max, new_R);
        }
        else
        {
            new_R.push_back(r);
        }
    }

    R = std::move(new_R);
    remove_non_maximal(R);
}

void remove_non_maximal(std::vector<Cuboid>& R) noexcept
{
    // O(N²) 剔除：若 r_i 完全包含 r_j，删 r_j
    size_t n = R.size();
    if (n <= 1)
    {
        return;
    }

    std::vector<bool> keep(n, true);

    for (size_t i = 0; i < n; ++i)
    {
        if (!keep[i])
        {
            continue;
        }
        for (size_t j = 0; j < n; ++j)
        {
            if (i == j || !keep[j])
            {
                continue;
            }
            if (R[i].contains(R[j]))
            {
                keep[j] = false;
            }
        }
    }

    size_t w = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (keep[i])
        {
            if (w != i)
            {
                R[w] = R[i];
            }
            ++w;
        }
    }
    R.resize(w);
}

} // namespace pack3d::bsg
