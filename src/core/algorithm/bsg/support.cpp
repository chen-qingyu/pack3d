#include "support.hpp"

#include <algorithm>

namespace pack3d::bsg
{

namespace
{

struct SupportState
{
    int32_t x_min = 0;
    int32_t x_max = 0;
    int32_t y_min = 0;
    int32_t y_max = 0;
    int32_t target_z = 0;
    int64_t supported_area = 0;
    int corner_mask = 0;
    bool directly_supported = false;
};

int find_block_index(const std::vector<GeneralBlock>& blocks, int64_t id) noexcept
{
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        if (blocks[i].id == id)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void add_support(const Position& position,
                 const OrientedSize& size,
                 SupportState& state) noexcept
{
    if (position.z + size.dz != state.target_z)
    {
        return;
    }

    int32_t ox_min = std::max(state.x_min, position.x);
    int32_t ox_max = std::min(state.x_max, position.x + size.dx);
    int32_t oy_min = std::max(state.y_min, position.y);
    int32_t oy_max = std::min(state.y_max, position.y + size.dy);
    if (ox_min >= ox_max || oy_min >= oy_max)
    {
        return;
    }

    state.directly_supported = true;

    if (state.x_min >= position.x && state.x_min < position.x + size.dx)
    {
        if (state.y_min >= position.y && state.y_min < position.y + size.dy)
        {
            state.corner_mask |= 1;
        }
        if (state.y_max > position.y && state.y_max <= position.y + size.dy)
        {
            state.corner_mask |= 2;
        }
    }
    if (state.x_max > position.x && state.x_max <= position.x + size.dx)
    {
        if (state.y_min >= position.y && state.y_min < position.y + size.dy)
        {
            state.corner_mask |= 4;
        }
        if (state.y_max > position.y && state.y_max <= position.y + size.dy)
        {
            state.corner_mask |= 8;
        }
    }
    state.supported_area += static_cast<int64_t>(ox_max - ox_min) * (oy_max - oy_min);
}

void add_block_support(const GeneralBlock& block,
                       const Position& position,
                       const GlobalContext& ctx,
                       SupportState& state) noexcept
{
    if (block.merge_axis == GeneralBlock::MergeAxis::None)
    {
        OrientedSize box_size = ctx.box_types[block.type_idx].size.orient(block.orientation);
        for (int z = 0; z < block.nz; ++z)
        {
            for (int y = 0; y < block.ny; ++y)
            {
                for (int x = 0; x < block.nx; ++x)
                {
                    Position box_position{
                        position.x + x * box_size.dx,
                        position.y + y * box_size.dy,
                        position.z + z * box_size.dz,
                    };
                    add_support(box_position, box_size, state);
                }
            }
        }
        return;
    }

    int left_index = find_block_index(ctx.blocks, block.source_left_id);
    int right_index = find_block_index(ctx.blocks, block.source_right_id);
    if (left_index < 0 || right_index < 0)
    {
        return;
    }

    const auto& left = ctx.blocks[left_index];
    Position right_position = position;
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

    add_block_support(left, position, ctx, state);
    add_block_support(ctx.blocks[right_index], right_position, ctx, state);
}

} // namespace

bool is_supported(const BSGState& state,
                  const Position& position,
                  const OrientedSize& size,
                  const GlobalContext& ctx) noexcept
{
    if (ctx.support_rate <= 0.0 || position.z == 0)
    {
        return true;
    }

    int64_t total_area = static_cast<int64_t>(size.dx) * size.dy;
    if (total_area <= 0)
    {
        return false;
    }

    SupportState support{
        position.x,
        position.x + size.dx,
        position.y,
        position.y + size.dy,
        position.z,
    };
    for (const auto& placed : state.placements)
    {
        if (placed.block_idx < 0 || static_cast<size_t>(placed.block_idx) >= ctx.blocks.size())
        {
            return false;
        }
        add_block_support(ctx.blocks[placed.block_idx], placed.anchor, ctx, support);
    }

    if (!support.directly_supported)
    {
        return false;
    }
    if (support.corner_mask == 15)
    {
        return true;
    }

    double ratio = static_cast<double>(support.supported_area) / static_cast<double>(total_area);
    return ratio + 1e-9 >= ctx.support_rate;
}

} // namespace pack3d::bsg
