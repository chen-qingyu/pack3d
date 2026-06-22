#include "grid.hpp"

#include <set>

namespace hypercube::rgs
{

int32_t compute_cell_size(
    const std::vector<Box>& boxes,
    const std::map<std::string, BoxType>& box_type_map) noexcept
{
    if (boxes.empty())
    {
        return 1;
    }

    int64_t sum = 0;
    for (const auto& bx : boxes)
    {
        auto it = box_type_map.find(bx.box_type_id);
        if (it == box_type_map.end())
        {
            continue;
        }
        const auto& sz = it->second.size;
        sum += static_cast<int64_t>(sz.x) + sz.y + sz.z;
    }

    int64_t n = static_cast<int64_t>(boxes.size());
    int32_t cell = static_cast<int32_t>(sum / (3 * n));
    return cell > 0 ? cell : 1;
}

void grid_register(
    EpContext& ctx,
    const std::vector<Placement>& placements,
    size_t idx) noexcept
{
    if (idx >= placements.size())
    {
        return;
    }

    const auto& pl = placements[idx];
    int32_t cs = ctx.grid_cell_size;

    int32_t cx0 = pl.position.x / cs;
    int32_t cx1 = (pl.position.x + pl.osize.dx - 1) / cs;
    int32_t cy0 = pl.position.y / cs;
    int32_t cy1 = (pl.position.y + pl.osize.dy - 1) / cs;
    int32_t cz0 = pl.position.z / cs;
    int32_t cz1 = (pl.position.z + pl.osize.dz - 1) / cs;

    for (int32_t cx = cx0; cx <= cx1; ++cx)
    {
        for (int32_t cy = cy0; cy <= cy1; ++cy)
        {
            for (int32_t cz = cz0; cz <= cz1; ++cz)
            {
                ctx.grid[{cx, cy, cz}].push_back(idx);
            }
        }
    }
}

std::vector<size_t> grid_neighbors(
    const EpContext& ctx,
    const std::vector<Placement>& /*placements*/,
    const Position& pos,
    const OrientedSize& osize) noexcept
{
    int32_t cs = ctx.grid_cell_size;
    int32_t cx0 = pos.x / cs;
    int32_t cx1 = (pos.x + osize.dx - 1) / cs;
    int32_t cy0 = pos.y / cs;
    int32_t cy1 = (pos.y + osize.dy - 1) / cs;
    int32_t cz0 = pos.z / cs;
    int32_t cz1 = (pos.z + osize.dz - 1) / cs;

    std::set<size_t> seen;
    for (int32_t cx = cx0; cx <= cx1; ++cx)
    {
        for (int32_t cy = cy0; cy <= cy1; ++cy)
        {
            for (int32_t cz = cz0; cz <= cz1; ++cz)
            {
                auto it = ctx.grid.find({cx, cy, cz});
                if (it != ctx.grid.end())
                {
                    for (size_t nidx : it->second)
                    {
                        seen.insert(nidx);
                    }
                }
            }
        }
    }
    return std::vector<size_t>(seen.begin(), seen.end());
}

bool grid_collides(
    const std::vector<Placement>& placements,
    const Position& pos,
    const OrientedSize& osize,
    const std::vector<size_t>& neighbors) noexcept
{
    int32_t a_x2 = pos.x + osize.dx;
    int32_t a_y2 = pos.y + osize.dy;
    int32_t a_z2 = pos.z + osize.dz;

    for (size_t idx : neighbors)
    {
        if (idx >= placements.size())
        {
            continue;
        }
        const auto& pl = placements[idx];
        int32_t b_x2 = pl.position.x + pl.osize.dx;
        int32_t b_y2 = pl.position.y + pl.osize.dy;
        int32_t b_z2 = pl.position.z + pl.osize.dz;

        if (pos.x >= b_x2 || pl.position.x >= a_x2)
        {
            continue;
        }
        if (pos.y >= b_y2 || pl.position.y >= a_y2)
        {
            continue;
        }
        if (pos.z >= b_z2 || pl.position.z >= a_z2)
        {
            continue;
        }
        return true;
    }
    return false;
}

} // namespace hypercube::rgs
