#include "packer.hpp"

#include <algorithm>
#include <cassert>
#include <map>
#include <queue>
#include <set>

#include "block.hpp"
#include "constraints.hpp"
#include "geometry.hpp"
#include "objectives.hpp"
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
                    if (!check_route_order_constraint(
                            sim, block.platform, pos, single, problem_.route.value()))
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

ContainerPacker::LocalPackScore ContainerPacker::score_state(const ContainerLoad& state) const
{
    LocalPackScore score;
    score.platform_count = static_cast<int>(state.platforms.size());
    score.group_count = static_cast<int>(state.groups.size());
    score.used_volume = state.used_volume;
    score.placed_count = static_cast<int>(state.placements.size());
    return score;
}

int ContainerPacker::compare_local_scores(const LocalPackScore& a,
                                          const LocalPackScore& b) const
{
    const auto& keys = problem_.objective_keys.empty()
                           ? default_objective_keys()
                           : problem_.objective_keys;

    for (const auto& key : keys)
    {
        if (key == "min_platform_count")
        {
            if (a.platform_count < b.platform_count)
            {
                return -1;
            }
            if (a.platform_count > b.platform_count)
            {
                return 1;
            }
        }
        else if (key == "max_volume_rate")
        {
            if (a.used_volume > b.used_volume)
            {
                return -1;
            }
            if (a.used_volume < b.used_volume)
            {
                return 1;
            }
        }
        else if (key == "min_group_split")
        {
            if (a.group_count < b.group_count)
            {
                return -1;
            }
            if (a.group_count > b.group_count)
            {
                return 1;
            }
        }
    }

    if (a.used_volume > b.used_volume)
    {
        return -1;
    }
    if (a.used_volume < b.used_volume)
    {
        return 1;
    }
    if (a.placed_count > b.placed_count)
    {
        return -1;
    }
    if (a.placed_count < b.placed_count)
    {
        return 1;
    }
    if (a.platform_count < b.platform_count)
    {
        return -1;
    }
    if (a.platform_count > b.platform_count)
    {
        return 1;
    }
    if (a.group_count < b.group_count)
    {
        return -1;
    }
    if (a.group_count > b.group_count)
    {
        return 1;
    }
    return 0;
}

const SimpleBlock* ContainerPacker::pick_best_block(
    const std::vector<const SimpleBlock*>& viable,
    const Space& space,
    const ContainerLoad& state,
    const std::map<std::string, int>& available,
    const std::vector<Space>& stack,
    const std::vector<SimpleBlock>& all_blocks,
    int eval_count) const
{
    assert(!viable.empty());

    const SimpleBlock* best = viable.front();
    LocalPackScore best_score;
    bool found = false;

    int limit = std::min(eval_count, static_cast<int>(viable.size()));
    for (int i = 0; i < limit; ++i)
    {
        ContainerLoad sim = state;
        auto sim_avail = available;
        auto sim_stack = stack;
        place_block(*viable[i], space, sim, sim_avail, sim_stack);

        LocalPackScore score = complete_largest(
            std::move(sim), std::move(sim_avail), std::move(sim_stack), all_blocks);
        int cmp = found ? compare_local_scores(score, best_score) : -1;
        if (!found || cmp < 0 ||
            (cmp == 0 && viable[i]->volume() > best->volume()))
        {
            found = true;
            best = viable[i];
            best_score = score;
        }
    }

    if (!state.placements.empty())
    {
        LocalPackScore current_score = score_state(state);
        if (compare_local_scores(current_score, best_score) <= 0)
        {
            return nullptr;
        }
    }

    return best;
}

PackResult ContainerPacker::pack_beam(const std::vector<Box>& boxes, int width)
{
    const int kKeepTopN = std::max(1, std::min(width, 16));
    const int kMaxRefineRounds = 6;

    std::map<std::string, int> all_available;
    for (const auto& bx : boxes)
    {
        all_available[bx.box_type_id]++;
    }

    auto all_blocks = block_gen_.generate_all(container_.inner_size, all_available);

    ContainerLoad state;
    state.type = &container_;
    state.type_id = container_.id;

    auto available = all_available;
    std::vector<Space> stack;
    stack.push_back({{0, 0, 0}, container_.inner_size.x, container_.inner_size.y, container_.inner_size.z});

    while (!stack.empty() && !available.empty() && !all_blocks.empty())
    {
        if (!check_time())
        {
            break;
        }

        all_blocks.erase(
            std::remove_if(all_blocks.begin(), all_blocks.end(),
                           [&](const SimpleBlock& b)
                           {
                               auto it = available.find(b.box_type_id);
                               return it == available.end() || it->second < b.box_count;
                           }),
            all_blocks.end());
        if (all_blocks.empty())
        {
            break;
        }

        Space space = stack.back();
        stack.pop_back();

        auto viable = filter_viable_blocks(all_blocks, space, available, state);
        if (viable.empty())
        {
            stack.push_back(space);
            if (!transfer_space(stack))
            {
                stack.pop_back();
            }
            continue;
        }

        int eval_limit = std::min(static_cast<int>(viable.size()), std::max(width, kKeepTopN * 4));
        std::vector<const SimpleBlock*> candidates(viable.begin(), viable.begin() + eval_limit);

        for (int round = 0; round < kMaxRefineRounds && candidates.size() > 1; ++round)
        {
            if (!check_time())
            {
                break;
            }

            struct CandidateScore
            {
                const SimpleBlock* block{nullptr};
                LocalPackScore fitness;
            };

            std::vector<CandidateScore> scored;
            scored.reserve(candidates.size());
            for (const SimpleBlock* block : candidates)
            {
                ContainerLoad sim = state;
                auto sim_avail = available;
                auto sim_stack = stack;
                place_block(*block, space, sim, sim_avail, sim_stack);

                LocalPackScore fitness = greedy_complete(
                    std::move(sim), std::move(sim_avail),
                    std::move(sim_stack), all_blocks);
                scored.push_back({block, fitness});
            }

            std::sort(scored.begin(), scored.end(),
                      [&](const CandidateScore& a, const CandidateScore& b)
                      {
                          int cmp = compare_local_scores(a.fitness, b.fitness);
                          if (cmp != 0)
                          {
                              return cmp < 0;
                          }
                          return a.block->volume() > b.block->volume();
                      });

            int next_size = 1;
            if (static_cast<int>(scored.size()) > 2 * kKeepTopN)
            {
                next_size = static_cast<int>((scored.size() + 1) / 2);
            }
            else
            {
                next_size = std::min(kKeepTopN, static_cast<int>(scored.size()));
            }

            candidates.clear();
            candidates.reserve(next_size);
            for (int i = 0; i < next_size; ++i)
            {
                candidates.push_back(scored[i].block);
            }
        }

        // 从 beam 精炼中选择最佳候选，并检查多目标提前停止
        const SimpleBlock* best = candidates.front();

        if (!state.placements.empty())
        {
            ContainerLoad sim = state;
            auto sim_avail = available;
            auto sim_stack = stack;
            place_block(*best, space, sim, sim_avail, sim_stack);
            LocalPackScore current_score = score_state(state);
            LocalPackScore new_score = complete_largest(
                std::move(sim), std::move(sim_avail), std::move(sim_stack), all_blocks);
            if (compare_local_scores(current_score, new_score) <= 0)
            {
                break;
            }
        }

        place_block(*best, space, state, available, stack);
    }

    std::map<std::string, std::vector<std::string>> real_ids_by_type;
    for (const auto& bx : boxes)
    {
        real_ids_by_type[bx.box_type_id].push_back(bx.id);
    }

    std::map<std::string, size_t> consume_idx;
    for (auto& pl : state.placements)
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

    return make_result(state, available, boxes);
}

ContainerPacker::LocalPackScore ContainerPacker::greedy_complete(
    ContainerLoad state,
    std::map<std::string, int> available,
    std::vector<Space> stack,
    const std::vector<SimpleBlock>& all_blocks) const
{
    const int kEvalWidth = 4;

    while (!stack.empty())
    {
        auto blocks = all_blocks;
        blocks.erase(
            std::remove_if(blocks.begin(), blocks.end(),
                           [&](const SimpleBlock& b)
                           {
                               auto it = available.find(b.box_type_id);
                               return it == available.end() || it->second < b.box_count;
                           }),
            blocks.end());
        if (blocks.empty())
        {
            break;
        }

        Space space = stack.back();
        stack.pop_back();

        auto viable = filter_viable_blocks(blocks, space, available, state);
        if (viable.empty())
        {
            stack.push_back(space);
            if (!transfer_space(stack))
            {
                stack.pop_back();
            }
            continue;
        }

        int eval_count = std::min(kEvalWidth, static_cast<int>(viable.size()));
        const SimpleBlock* best = pick_best_block(
            viable, space, state, available, stack, blocks, eval_count);
        if (best == nullptr)
        {
            break;
        }

        place_block(*best, space, state, available, stack);
    }

    return score_state(state);
}

ContainerPacker::LocalPackScore ContainerPacker::complete_largest(
    ContainerLoad state,
    std::map<std::string, int> available,
    std::vector<Space> stack,
    const std::vector<SimpleBlock>& all_blocks) const
{
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
                stack.pop_back();
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

    return score_state(state);
}

} // namespace hypercube
