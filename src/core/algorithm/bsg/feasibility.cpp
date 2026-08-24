#include "feasibility.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

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
    if (block_idx < 0 || static_cast<size_t>(block_idx) >= ctx.blocks.size())
    {
        return false;
    }

    std::vector<LeafItem> leaves;
    append_block_leaves(block_idx, position, ctx, ctx.block_indices, leaves);
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

    // tender 约束：逐叶按 group 去重检查（复合块可能含多个 group），
    // 通过后把 group 记入 next_load.groups，供后续块判定连通
    // 全支撑（support_rate>=1）下不存在"下方留空隙、后放下方箱"的乱序放置，可跳过检测
    const bool need_recompute_check = ctx.support_rate < 1.0;
    bool need_recompute = false;
    for (const auto& leaf : leaves)
    {
        if (leaf.item_class_idx < 0 || static_cast<size_t>(leaf.item_class_idx) >= ctx.item_classes.size())
        {
            return false;
        }
        const auto& item = ctx.item_classes[leaf.item_class_idx];
        const auto item_groups = effective_groups(item.group_members, item.group);
        if (ctx.tender.limit > 0 && !item_groups.empty())
        {
            if (!check_tender_limit(ctx.tender, next_load.groups, item_groups))
            {
                return false;
            }
            next_load.groups.insert(item_groups.begin(), item_groups.end());
        }
        if (!check_boundary(ctx.container_type, leaf.position, leaf.osize) ||
            check_overlap(leaf.position, leaf.osize, next_load.placements) ||
            check_obstacle(leaf.position, leaf.osize, ctx.container_type.obstacles) ||
            check_facet(leaf.position, leaf.osize, ctx.container_size,
                        ctx.container_type.facets))
        {
            return false;
        }
        if (ctx.has_weight_info && !check_weight(next_load, item.weight))
        {
            return false;
        }
        if (!check_support(leaf.position, leaf.osize, next_load, ctx.support_rate))
        {
            return false;
        }
        if ((ctx.has_max_stack || ctx.has_max_load) &&
            !check_stack_constraints(leaf.position, leaf.osize, item.weight,
                                     next_load, ctx.box_type_map))
        {
            return false;
        }
        if (ctx.heavy_not_on_light &&
            !check_heavy_not_on_light(leaf.position, leaf.osize, item.weight, next_load))
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

        next_load.placements.push_back({"", item.box_type_id, "", leaf.position,
                                        leaf.orientation, leaf.osize, item.platform, ""});
        next_load.placements.back().weight = item.weight;
        next_load.placements.back().group_members = item_groups;
        if (ctx.has_max_stack || ctx.has_max_load)
        {
            apply_stack_state(leaf.position, leaf.osize, item.weight, next_load);
        }
        next_load.used_volume += leaf.osize.volume();
        next_load.total_weight += item.weight;
        if (!item.platform.empty())
        {
            next_load.platforms.insert(item.platform);
        }
        next_item_classes.push_back(leaf.item_class_idx);

        // 乱序放置检测：本叶上方已有箱子（其底面 == 本叶顶面且投影相交）→ 需整体重算
        if (!need_recompute && need_recompute_check)
        {
            const int32_t top = leaf.position.z + leaf.osize.dz;
            for (const auto& ep : next_load.placements)
            {
                if (ep.position.z == top &&
                    ep.position.x < leaf.position.x + leaf.osize.dx &&
                    leaf.position.x < ep.position.x + ep.osize.dx &&
                    ep.position.y < leaf.position.y + leaf.osize.dy &&
                    leaf.position.y < ep.position.y + ep.osize.dy)
                {
                    need_recompute = true;
                    break;
                }
            }
        }
    }

    // 仅乱序放置（悬空箱先放、下方箱后放）时整体重建并校验；
    // 正常自底向上堆叠时增量状态已正确，跳过重算。
    if (need_recompute)
    {
        std::vector<std::string> stack_errs;
        recompute_stack_state(next_load, ctx.box_type_map, &stack_errs);
        if (!stack_errs.empty())
        {
            return false;
        }
    }
    return true;
}

} // namespace pack3d::bsg
