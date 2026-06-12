#include "packer.hpp"

#include <algorithm>
#include <cassert>
#include <map>
#include <queue>
#include <set>

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
            // 选体积最大的可行块
            const SimpleBlock* best = candidates.front();

            // 用 place_block 放置（使用占位 ID）
            size_t before = state.placements.size();
            place_block(*best, space, state, available, stack);

            // 将占位 ID 替换为真实箱子 ID
            auto& type_boxes = boxes_by_type[best->box_type_id];
            int bi = 0;
            for (size_t pi = before; pi < state.placements.size(); ++pi)
            {
                if (bi < static_cast<int>(type_boxes.size()))
                {
                    state.placements[pi].box_id = type_boxes[bi]->id;
                    result.placements.push_back(state.placements[pi]);
                    ++bi;
                }
            }
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

void ContainerPacker::place_block(
    const SimpleBlock& block, const Space& space,
    ContainerLoad& state,
    std::map<std::string, int>& available,
    std::vector<Space>& stack) const
{
    auto single = orient_size(box_type_map_.at(block.box_type_id).size, block.orientation);

    // 找到 box_type_id 对应的箱子（用于获取 platform/group/weight）
    const Box* sample_box = nullptr;
    for (const auto& [id, bx] : box_map_)
    {
        if (bx.box_type_id == block.box_type_id)
        {
            sample_box = &bx;
            break;
        }
    }

    int placed = 0;
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

                Placement pl;
                pl.box_id = std::string("__block_") + std::to_string(placed++);
                pl.box_type_id = block.box_type_id;
                pl.position = pos;
                pl.orientation = block.orientation;

                state.placements.push_back(std::move(pl));
                state.used_volume += single.volume();
                if (has_weight_info_ && sample_box && sample_box->weight.has_value())
                {
                    state.total_weight += sample_box->weight.value();
                }

                if (sample_box && !sample_box->platform.empty())
                {
                    state.platforms.insert(sample_box->platform);
                    int32_t box_max_x = pos.x + single.dx;
                    auto xmax_it = state.platform_x_max.find(sample_box->platform);
                    if (xmax_it == state.platform_x_max.end() || box_max_x > xmax_it->second)
                    {
                        state.platform_x_max[sample_box->platform] = box_max_x;
                    }
                    int32_t box_min_x = pos.x;
                    auto xmin_it = state.platform_x_min.find(sample_box->platform);
                    if (xmin_it == state.platform_x_min.end() || box_min_x < xmin_it->second)
                    {
                        state.platform_x_min[sample_box->platform] = box_min_x;
                    }
                }

                if (sample_box && !sample_box->group.empty())
                {
                    state.groups.insert(sample_box->group);
                }
            }
        }
    }

    available[block.box_type_id] -= block.box_count;
    if (available[block.box_type_id] <= 0)
    {
        available.erase(block.box_type_id);
    }

    split_space(space, block.osize, stack);
}

// ===== Beam 搜索 =====

PackResult ContainerPacker::make_result(
    const ContainerLoad& state,
    const std::map<std::string, int>& /*available*/,
    const std::vector<Box>& all_boxes) const
{
    PackResult r;
    r.used_volume = state.used_volume;
    r.total_weight = state.total_weight;
    r.platforms = state.platforms;
    r.groups = state.groups;
    r.platform_x_max = state.platform_x_max;
    r.platform_x_min = state.platform_x_min;
    r.placements = state.placements;

    std::set<std::string> packed;
    for (const auto& pl : state.placements)
    {
        packed.insert(pl.box_id);
    }
    for (const auto& bx : all_boxes)
    {
        if (!packed.count(bx.id))
        {
            r.unpacked_box_ids.push_back(bx.id);
        }
    }
    r.success = (r.placements.size() == all_boxes.size());
    return r;
}

PackResult ContainerPacker::pack_beam(const std::vector<Box>& boxes, int beam_width)
{
    std::map<std::string, int> all_available;
    for (const auto& bx : boxes)
    {
        all_available[bx.box_type_id]++;
    }

    auto all_blocks = block_gen_.generate_all(container_.inner_size, all_available);

    std::vector<PartialState> beam(1);
    beam[0].state.type = &container_;
    beam[0].state.type_id = container_.id;
    beam[0].available = all_available;
    beam[0].stack.push_back({{0, 0, 0}, container_.inner_size.x, container_.inner_size.y, container_.inner_size.z});

    while (!beam.empty())
    {
        bool all_done = true;
        for (const auto& ps : beam)
        {
            if (!ps.stack.empty() && !ps.available.empty())
            {
                all_done = false;
                break;
            }
        }
        if (all_done)
            break;

        std::vector<PartialState> candidates;

        for (const auto& ps : beam)
        {
            if (ps.stack.empty() || ps.available.empty())
            {
                candidates.push_back(ps);
                continue;
            }

            auto live_blocks = all_blocks;
            live_blocks.erase(
                std::remove_if(live_blocks.begin(), live_blocks.end(),
                               [&](const SimpleBlock& b)
                               {
                                   auto it = ps.available.find(b.box_type_id);
                                   return it == ps.available.end() || it->second < b.box_count;
                               }),
                live_blocks.end());

            if (live_blocks.empty())
            {
                candidates.push_back(ps);
                continue;
            }

            Space space = ps.stack.back();
            auto viable = filter_viable_blocks(live_blocks, space, ps.available, ps.state);

            if (viable.empty())
            {
                auto mut_stack = ps.stack;
                mut_stack.push_back(space);
                if (!transfer_space(mut_stack))
                    mut_stack.pop_back();
                PartialState next = ps;
                next.stack = std::move(mut_stack);
                candidates.push_back(std::move(next));
                continue;
            }

            // 前瞻评估：对候选块打分，选 top beam_width
            std::vector<double> scores;
            int effort = problem_.solver_config.effort;
            evaluate_blocks(viable, space, ps.state, ps.available,
                            live_blocks, effort > 0 ? effort : beam_width * 2, scores);

            // 按分数降序排列
            std::vector<size_t> indices(viable.size());
            for (size_t i = 0; i < indices.size(); ++i)
                indices[i] = i;
            std::sort(indices.begin(), indices.end(),
                      [&](size_t a, size_t b)
                      { return scores[a] > scores[b]; });

            int n = std::min(beam_width, static_cast<int>(viable.size()));
            for (int ci = 0; ci < n; ++ci)
            {
                PartialState next = ps;
                auto mut_stack = ps.stack;
                mut_stack.pop_back();
                place_block(*viable[indices[ci]], space, next.state, next.available, mut_stack);
                next.stack = std::move(mut_stack);
                next.boxes_placed = static_cast<int>(next.state.placements.size());
                candidates.push_back(std::move(next));
            }
        }

        std::partial_sort(candidates.begin(),
                          candidates.begin() + std::min(beam_width, static_cast<int>(candidates.size())),
                          candidates.end(),
                          [](const PartialState& a, const PartialState& b)
                          {
                              return a.boxes_placed > b.boxes_placed;
                          });

        beam.resize(std::min(beam_width, static_cast<int>(candidates.size())));
        for (int i = 0; i < static_cast<int>(beam.size()); ++i)
        {
            beam[i] = std::move(candidates[i]);
        }
    }

    if (beam.empty())
    {
        return make_result(ContainerLoad{}, all_available, boxes);
    }

    std::partial_sort(beam.begin(), beam.begin() + 1, beam.end(),
                      [](const PartialState& a, const PartialState& b)
                      {
                          return a.boxes_placed > b.boxes_placed;
                      });

    auto& best_state = beam[0].state;

    std::map<std::string, std::vector<std::string>> real_ids_by_type;
    for (const auto& bx : boxes)
    {
        real_ids_by_type[bx.box_type_id].push_back(bx.id);
    }

    std::map<std::string, size_t> consume_idx;
    for (auto& pl : best_state.placements)
    {
        if (pl.box_id.find("__block_") == 0)
        {
            auto& ids = real_ids_by_type[pl.box_type_id];
            size_t& idx = consume_idx[pl.box_type_id];
            if (idx < ids.size())
            {
                pl.box_id = ids[idx++];
            }
        }
    }

    return make_result(best_state, beam[0].available, boxes);
}

double ContainerPacker::greedy_complete(
    ContainerLoad state,
    std::map<std::string, int> available,
    std::vector<Space> stack,
    const std::vector<SimpleBlock>& all_blocks) const
{
    int64_t total_vol = container_.inner_size.volume();
    if (total_vol <= 0)
        return 0.0;

    auto blocks = all_blocks;

    while (!stack.empty() && !blocks.empty())
    {
        Space space = stack.back();
        stack.pop_back();

        auto viable = filter_viable_blocks(blocks, space, available, state);

        if (!viable.empty())
        {
            place_block(*viable[0], space, state, available, stack);
        }
        else
        {
            stack.push_back(space);
            if (!transfer_space(stack))
            {
                stack.pop_back();
            }
        }

        blocks.erase(
            std::remove_if(blocks.begin(), blocks.end(),
                           [&](const SimpleBlock& b)
                           {
                               auto it = available.find(b.box_type_id);
                               return it == available.end() || it->second < b.box_count;
                           }),
            blocks.end());
    }

    return static_cast<double>(state.used_volume) / static_cast<double>(total_vol);
}

void ContainerPacker::evaluate_blocks(
    const std::vector<const SimpleBlock*>& viable,
    const Space& space,
    const ContainerLoad& state,
    const std::map<std::string, int>& available,
    const std::vector<SimpleBlock>& all_blocks,
    int effort,
    std::vector<double>& scores) const
{
    scores.resize(viable.size(), 0.0);

    // effort=0: 仅按块体积打分（快速）
    if (effort <= 0)
    {
        for (size_t i = 0; i < viable.size(); ++i)
        {
            scores[i] = static_cast<double>(viable[i]->volume());
        }
        return;
    }

    int eval_count = std::min(effort, static_cast<int>(viable.size()));

    for (int i = 0; i < eval_count; ++i)
    {
        // 模拟放置
        ContainerLoad sim_state = state;
        auto sim_avail = available;
        std::vector<Space> sim_stack = {space};

        place_block(*viable[i], space, sim_state, sim_avail, sim_stack);

        // 贪心完成
        double fill_rate = greedy_complete(
            std::move(sim_state), std::move(sim_avail),
            std::move(sim_stack), all_blocks);

        scores[i] = fill_rate * 100.0; // 百分比作为分数
    }

    // 超出 effort 的候选按体积打分
    for (size_t i = static_cast<size_t>(eval_count); i < viable.size(); ++i)
    {
        scores[i] = static_cast<double>(viable[i]->volume()) / 1000.0;
    }
}

} // namespace hypercube
