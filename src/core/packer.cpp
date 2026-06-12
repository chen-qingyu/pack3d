#include "packer.hpp"

#include <algorithm>
#include <cassert>
#include <map>

#include "block.hpp"
#include "constraints.hpp"
#include "geometry.hpp"
#include "space.hpp"

namespace hypercube
{

ContainerPacker::ContainerPacker(
    const ContainerType& container,
    const std::map<std::string, BoxType>& box_type_map,
    const std::map<std::string, Box>& box_map,
    const Problem& problem,
    bool has_weight_info)
    : container_(container)
    , box_type_map_(box_type_map)
    , box_map_(box_map)
    , problem_(problem)
    , has_weight_info_(has_weight_info)
    , block_gen_(box_type_map)
{
}

PackResult ContainerPacker::pack(const std::vector<Box>& boxes)
{
    PackResult result;

    // 可用箱子计数（按 box_type_id）
    std::map<std::string, int> available;
    // 可用箱子列表（按 box_type_id 分组，存原始指针）
    std::map<std::string, std::vector<const Box*>> boxes_by_type;
    for (const auto& bx : boxes)
    {
        available[bx.box_type_id]++;
        boxes_by_type[bx.box_type_id].push_back(&bx);
    }

    // 生成块表
    auto all_blocks = block_gen_.generate_all(container_.inner_size, available);

    // 初始化容器状态
    ContainerLoad state;
    state.type = &container_;
    state.type_id = container_.id;

    // 空间栈从完整容器开始
    std::vector<Space> stack;
    stack.push_back({{0, 0, 0}, container_.inner_size.x, container_.inner_size.y, container_.inner_size.z});

    while (!stack.empty() && !all_blocks.empty())
    {
        Space space = stack.back();
        stack.pop_back();

        // 筛选可行块
        auto candidates = filter_viable_blocks(all_blocks, space, available, state);

        if (!candidates.empty())
        {
            // 选体积最大的可行块（已按体积降序排列）
            const SimpleBlock* best = candidates.front();

            // 展开块为单个箱子 placement
            auto box_osize = block_gen_.generate_for_type(
                best->box_type_id, container_.inner_size, "", "", 1);
            // 取第一个朝向的单个箱子尺寸
            auto single = orient_size(box_type_map_.at(best->box_type_id).size, best->orientation);

            // 从可用箱子中取出具体 ID
            auto& type_boxes = boxes_by_type[best->box_type_id];
            int taken = 0;

            for (int iz = 0; iz < best->nz; ++iz)
            {
                for (int iy = 0; iy < best->ny; ++iy)
                {
                    for (int ix = 0; ix < best->nx; ++ix)
                    {
                        if (taken >= static_cast<int>(type_boxes.size()))
                        {
                            break;
                        }

                        const Box* box_ptr = type_boxes[taken];
                        ++taken;

                        Position pos;
                        pos.x = space.pos.x + ix * single.dx;
                        pos.y = space.pos.y + iy * single.dy;
                        pos.z = space.pos.z + iz * single.dz;

                        Placement pl;
                        pl.box_id = box_ptr->id;
                        pl.box_type_id = best->box_type_id;
                        pl.position = pos;
                        pl.orientation = best->orientation;

                        // 更新容器状态
                        state.placements.push_back(pl);
                        state.used_volume += single.volume();
                        if (has_weight_info_)
                        {
                            state.total_weight += box_ptr->weight.value_or(0.0);
                        }

                        // 更新平台跟踪
                        if (!box_ptr->platform.empty())
                        {
                            state.platforms.insert(box_ptr->platform);
                            int32_t box_max_x = pos.x + single.dx;
                            auto xmax_it = state.platform_x_max.find(box_ptr->platform);
                            if (xmax_it == state.platform_x_max.end() || box_max_x > xmax_it->second)
                            {
                                state.platform_x_max[box_ptr->platform] = box_max_x;
                            }
                            int32_t box_min_x = pos.x;
                            auto xmin_it = state.platform_x_min.find(box_ptr->platform);
                            if (xmin_it == state.platform_x_min.end() || box_min_x < xmin_it->second)
                            {
                                state.platform_x_min[box_ptr->platform] = box_min_x;
                            }
                        }

                        // 更新分组
                        if (!box_ptr->group.empty())
                        {
                            state.groups.insert(box_ptr->group);
                        }

                        result.placements.push_back(std::move(pl));
                    }
                }
            }

            // 更新可用库存
            available[best->box_type_id] -= taken;
            // 移除已用尽的类型
            if (available[best->box_type_id] <= 0)
            {
                available.erase(best->box_type_id);
            }

            // 划分剩余空间
            split_space(space, best->osize, stack);
        }
        else
        {
            // 无可行块，尝试空间回收
            stack.push_back(space);
            if (!transfer_space(stack))
            {
                // 回收失败，丢弃此空间
                stack.pop_back();
            }
        }

        // 更新块表（剔除库存不足的块）
        all_blocks.erase(
            std::remove_if(all_blocks.begin(), all_blocks.end(),
                           [&](const SimpleBlock& b)
                           {
                               return available.find(b.box_type_id) == available.end() ||
                                      available.at(b.box_type_id) < b.box_count;
                           }),
            all_blocks.end());
    }

    // 记录未装箱的箱子
    for (const auto& bx : boxes)
    {
        bool placed = false;
        for (const auto& pl : result.placements)
        {
            if (pl.box_id == bx.id)
            {
                placed = true;
                break;
            }
        }
        if (!placed)
        {
            result.unpacked_box_ids.push_back(bx.id);
        }
    }

    result.success = result.placements.size() == boxes.size();
    result.used_volume = state.used_volume;
    result.total_weight = state.total_weight;
    result.platforms = std::move(state.platforms);
    result.groups = std::move(state.groups);
    result.platform_x_max = std::move(state.platform_x_max);
    result.platform_x_min = std::move(state.platform_x_min);

    return result;
}

std::vector<const SimpleBlock*> ContainerPacker::filter_viable_blocks(
    const std::vector<SimpleBlock>& all_blocks,
    const Space& space,
    const std::map<std::string, int>& available,
    const ContainerLoad& state) const
{
    std::vector<const SimpleBlock*> viable;

    for (const auto& block : all_blocks)
    {
        // 库存检查
        auto it = available.find(block.box_type_id);
        if (it == available.end() || it->second < block.box_count)
        {
            continue;
        }

        // 尺寸检查：块能否放入当前空间
        if (block.osize.dx > space.lx ||
            block.osize.dy > space.ly ||
            block.osize.dz > space.lz)
        {
            continue;
        }

        // 约束检查
        if (!check_block_feasible(block, space, state))
        {
            continue;
        }

        viable.push_back(&block);
    }

    return viable;
}

bool ContainerPacker::check_block_feasible(
    const SimpleBlock& block,
    const Space& space,
    const ContainerLoad& state) const
{
    auto single = orient_size(box_type_map_.at(block.box_type_id).size, block.orientation);

    // 预取该类型单箱重量（用于重量检查）
    double box_weight = 0.0;
    if (has_weight_info_)
    {
        for (const auto& [id, bx] : box_map_)
        {
            if (bx.box_type_id == block.box_type_id && bx.weight.has_value())
            {
                box_weight = bx.weight.value();
                break;
            }
        }
    }

    // 模拟放置块内所有箱子，逐箱检查约束
    ContainerLoad sim = state;

    for (int iz = 0; iz < block.nz; ++iz)
    {
        for (int iy = 0; iy < block.ny; ++iy)
        {
            for (int ix = 0; ix < block.nx; ++ix)
            {
                Position pos;
                pos.x = space.pos.x + ix * single.dx;
                pos.y = space.pos.y + iy * single.dy;
                pos.z = space.pos.z + iz * single.dz;

                // 边界检查
                if (!check_boundary(container_, pos, single))
                {
                    return false;
                }

                // 重叠检查
                if (check_overlap_any(pos, single, sim.placements, box_type_map_))
                {
                    return false;
                }

                // 重量检查
                if (has_weight_info_ && container_.max_weight.has_value())
                {
                    if (sim.total_weight + box_weight > container_.max_weight.value() + 1e-9)
                    {
                        return false;
                    }
                    sim.total_weight += box_weight;
                }

                // 支撑率检查
                if (problem_.support_rate > 0.0 && pos.z > 0)
                {
                    double ratio = calc_support_ratio(pos, single, sim, box_type_map_);
                    if (ratio + 1e-9 < problem_.support_rate)
                    {
                        return false;
                    }
                }

                // 平台数量限制检查
                // 在 filter_viable_blocks 层面已检查库存，这里只需知道这个块的 platform
                // 但如果块内的平台在容器中还不存在，检查是否会超限
                if (!block.platform.empty() && problem_.platform_limit.has_value() && problem_.platform_limit.value() > 0)
                {
                    if (!sim.platforms.count(block.platform) &&
                        static_cast<int>(sim.platforms.size()) >= problem_.platform_limit.value())
                    {
                        return false;
                    }
                }

                // 路线顺序检查
                if (!block.platform.empty() && problem_.route.has_value())
                {
                    auto rr = check_route_order_constraint(
                        sim, block.platform, pos, single, problem_.route.value());
                    if (!rr.ok)
                    {
                        return false;
                    }
                }

                // 记录这个箱子的放置（供后续箱子的重叠/支撑检查）
                Placement pl;
                pl.box_id = "simulated";
                pl.box_type_id = block.box_type_id;
                pl.position = pos;
                pl.orientation = block.orientation;
                sim.placements.push_back(std::move(pl));

                // 更新平台跟踪
                if (!block.platform.empty())
                {
                    sim.platforms.insert(block.platform);
                    int32_t box_max_x = pos.x + single.dx;
                    auto xmax_it = sim.platform_x_max.find(block.platform);
                    if (xmax_it == sim.platform_x_max.end() || box_max_x > xmax_it->second)
                    {
                        sim.platform_x_max[block.platform] = box_max_x;
                    }
                    int32_t box_min_x = pos.x;
                    auto xmin_it = sim.platform_x_min.find(block.platform);
                    if (xmin_it == sim.platform_x_min.end() || box_min_x < xmin_it->second)
                    {
                        sim.platform_x_min[block.platform] = box_min_x;
                    }
                }
            }
        }
    }

    return true;
}

} // namespace hypercube
