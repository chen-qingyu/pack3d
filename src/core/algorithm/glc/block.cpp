#include "block.hpp"

#include <algorithm>
#include <cassert>

#include "../../constraints.hpp"

namespace hypercube::glc
{

BlockGenerator::BlockGenerator(const std::map<std::string, BoxType>& box_type_map)
    : box_type_map_(box_type_map)
{
}

std::vector<SimpleBlock> BlockGenerator::generate_for_type(
    const std::string& box_type_id,
    const Size& container_size,
    const std::string& platform,
    const std::string& group,
    int available_count) const
{
    std::vector<SimpleBlock> blocks;

    auto it = box_type_map_.find(box_type_id);
    if (it == box_type_map_.end())
    {
        return blocks;
    }

    const auto& bt = it->second;

    for (auto orient : bt.allowed_orientations)
    {
        auto os = bt.size.orient(orient);

        // 至少在一个方向上能放入
        if (os.dx > container_size.x || os.dy > container_size.y || os.dz > container_size.z)
        {
            continue;
        }

        int max_nx = container_size.x / os.dx;
        int max_ny = container_size.y / os.dy;
        int max_nz = container_size.z / os.dz;

        for (int nx = 1; nx <= max_nx; ++nx)
        {
            for (int ny = 1; ny <= max_ny; ++ny)
            {
                for (int nz = 1; nz <= max_nz; ++nz)
                {
                    int count = nx * ny * nz;
                    if (count > available_count)
                    {
                        continue;
                    }

                    SimpleBlock block;
                    block.box_type_id = box_type_id;
                    block.orientation = orient;
                    block.nx = nx;
                    block.ny = ny;
                    block.nz = nz;
                    block.box_count = count;
                    block.osize = {os.dx * nx, os.dy * ny, os.dz * nz};
                    block.platform = platform;
                    block.group = group;

                    blocks.push_back(std::move(block));
                }
            }
        }
    }

    return blocks;
}

void sort_blocks_by_volume_desc(std::vector<SimpleBlock>& blocks) noexcept
{
    std::sort(blocks.begin(), blocks.end(),
              [](const SimpleBlock& a, const SimpleBlock& b) noexcept
              {
                  return a.volume() > b.volume();
              });
}

} // namespace hypercube::glc
