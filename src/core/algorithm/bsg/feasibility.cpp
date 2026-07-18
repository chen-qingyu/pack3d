#include "feasibility.hpp"

#include <algorithm>
#include <unordered_map>

#include "../../constraints.hpp"

namespace pack3d::bsg
{

namespace
{

struct LeafItem
{
    int item_class_idx = -1;
    Position position;
    Orientation orientation = Orientation::XYZ;
    OrientedSize osize;
};

void append_block_leaves(
    int block_idx,
    const Position& base_position,
    const GlobalContext& ctx,
    const std::unordered_map<int64_t, int>& block_indices,
    std::vector<LeafItem>& leaves) noexcept
{
    const auto& block = ctx.blocks[block_idx];
    if (block.merge_axis == GeneralBlock::MergeAxis::None)
    {
        const auto size = ctx.box_types[block.type_idx].size.orient(block.orientation);
        for (int z = 0; z < block.nz; ++z)
        {
            for (int y = 0; y < block.ny; ++y)
            {
                for (int x = 0; x < block.nx; ++x)
                {
                    leaves.push_back({
                        block.type_idx,
                        {base_position.x + x * size.dx, base_position.y + y * size.dy, base_position.z + z * size.dz},
                        block.orientation,
                        size,
                    });
                }
            }
        }
        return;
    }

    const int left_idx = block_indices.at(block.source_left_id);
    const int right_idx = block_indices.at(block.source_right_id);
    Position right_position = base_position;
    const auto& left = ctx.blocks[left_idx];
    switch (block.merge_axis)
    {
        case GeneralBlock::MergeAxis::X:
            right_position.x += left.osize.dx;
            break;
        case GeneralBlock::MergeAxis::Y:
            right_position.y += left.osize.dy;
            break;
        case GeneralBlock::MergeAxis::Z:
            right_position.z += left.osize.dz;
            break;
        default:
            return;
    }

    append_block_leaves(left_idx, base_position, ctx, block_indices, leaves);
    append_block_leaves(right_idx, right_position, ctx, block_indices, leaves);
}

} // namespace

bool can_place_block(
    const BSGState& state,
    int block_idx,
    const Position& position,
    const GlobalContext& ctx,
    ContainerLoad& next_load,
    std::vector<int>& next_item_classes) noexcept
{
    if (ctx.item_classes.empty() || block_idx < 0 || static_cast<size_t>(block_idx) >= ctx.blocks.size())
    {
        return false;
    }

    std::unordered_map<int64_t, int> block_indices;
    block_indices.reserve(ctx.blocks.size());
    for (size_t index = 0; index < ctx.blocks.size(); ++index)
    {
        block_indices.emplace(ctx.blocks[index].id, static_cast<int>(index));
    }

    std::vector<LeafItem> leaves;
    append_block_leaves(block_idx, position, ctx, block_indices, leaves);
    std::sort(leaves.begin(), leaves.end(), [](const LeafItem& a, const LeafItem& b)
              {
        if (a.position.z != b.position.z)
        {
            return a.position.z < b.position.z;
        }
        if (a.position.y != b.position.y)
        {
            return a.position.y < b.position.y;
        }
        return a.position.x < b.position.x; });

    next_load = state.constraint_load;
    next_item_classes = state.item_class_indices;
    for (const auto& leaf : leaves)
    {
        if (leaf.item_class_idx < 0 || static_cast<size_t>(leaf.item_class_idx) >= ctx.item_classes.size())
        {
            return false;
        }
        const auto& item = ctx.item_classes[leaf.item_class_idx];
        if (!check_boundary(ctx.container_type, leaf.position, leaf.osize) ||
            check_overlap(leaf.position, leaf.osize, next_load.placements))
        {
            return false;
        }
        if (ctx.has_weight_info && !check_weight(next_load, item.weight))
        {
            return false;
        }
        if (!check_support(leaf.position, leaf.osize, next_load, ctx.box_type_map, ctx.support_rate))
        {
            return false;
        }
        if (ctx.platform_limit.has_value() &&
            !check_platform_limit(next_load, item.platform, ctx.platform_limit.value()))
        {
            return false;
        }
        if (ctx.route.has_value() &&
            !check_route_order(next_load, item.platform, leaf.position, leaf.osize, ctx.route.value()))
        {
            return false;
        }

        next_load.placements.push_back({"", item.box_type_id, "", leaf.position, leaf.orientation, leaf.osize, item.platform, ""});
        next_load.used_volume += leaf.osize.volume();
        next_load.total_weight += item.weight;
        if (!item.platform.empty())
        {
            next_load.platforms.insert(item.platform);
        }
        next_item_classes.push_back(leaf.item_class_idx);
    }
    return true;
}

} // namespace pack3d::bsg
