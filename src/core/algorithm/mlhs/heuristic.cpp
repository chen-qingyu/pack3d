#include "heuristic.hpp"

#include <algorithm>
#include <cassert>
#include <map>
#include <queue>
#include <set>

#include "../../constraints.hpp"
#include "../../objectives.hpp"
#include "../../tool.hpp"
#include "block.hpp"
#include "space.hpp"

namespace hypercube::mlhs
{

Heuristic::Heuristic(
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
    std::map<std::string, std::pair<int, double>> weight_accum;
    for (const auto& [id, bx] : box_map_)
    {
        if (!bx.weight.has_value())
            continue;
        auto pk = bx.box_type_id + "\t" + bx.platform + "\t" + bx.group;
        weight_accum[pk].first++;
        weight_accum[pk].second += bx.weight.value();
    }
    for (const auto& [pk, acc] : weight_accum)
    {
        type_avg_weight_[pk] = acc.second / acc.first;
    }
}

std::vector<const SimpleBlock*> Heuristic::filter_viable_blocks(
    const std::vector<SimpleBlock>& all_blocks,
    const Space& space,
    const std::map<std::string, std::vector<double>>& available,
    const ContainerLoad& state) const
{
    std::vector<const SimpleBlock*> viable;

    for (const auto& block : all_blocks)
    {
        // 库存检查
        auto it = available.find(block.box_type_id);
        if (it == available.end() || it->second.size() < static_cast<size_t>(block.box_count))
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

bool Heuristic::check_block_feasible(
    const SimpleBlock& block,
    const Space& space,
    const ContainerLoad& state) const
{
    auto single = box_type_map_.at(block.box_type_id).size.orient(block.orientation);

    // ---- per-block 检查 ----

    // 重量：按该组平均重量检查块总重量
    if (has_weight_info_ && container_.max_weight.has_value())
    {
        auto it = type_avg_weight_.find(block.box_type_id + "\t" + block.platform + "\t" + block.group);
        if (it != type_avg_weight_.end() &&
            !check_weight(state, it->second * static_cast<double>(block.box_count)))
        {
            return false;
        }
    }

    // 平台限制：块内箱子同平台，只查一次
    bool need_route = false;
    if (!block.platform.empty())
    {
        if (problem_.platform_limit.has_value() && problem_.platform_limit.value() > 0)
        {
            if (!check_platform_limit(state, block.platform, problem_.platform_limit.value()))
            {
                return false;
            }
        }
        need_route = problem_.route.has_value();
    }

    // 预计算块的 x 范围（供路线/平台跟踪用，避免逐箱重复计算）
    int32_t block_min_x = space.pos.x;
    int32_t block_max_x = space.pos.x + block.nx * single.dx;

    // ---- 逐箱检查：边界、重叠、支撑、路线 ----
    ContainerLoad sim = state;
    if (!block.platform.empty())
    {
        sim.platforms.insert(block.platform);
        auto xmax_it = sim.platform_x_max.find(block.platform);
        if (xmax_it == sim.platform_x_max.end() || block_max_x > xmax_it->second)
        {
            sim.platform_x_max[block.platform] = block_max_x;
        }
        auto xmin_it = sim.platform_x_min.find(block.platform);
        if (xmin_it == sim.platform_x_min.end() || block_min_x < xmin_it->second)
        {
            sim.platform_x_min[block.platform] = block_min_x;
        }
    }

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

                if (!check_boundary(container_, pos, single))
                {
                    return false;
                }

                // 重叠检查仅需查 state.placements（块内箱子网格排列，互不重叠）
                if (check_overlap(pos, single, state.placements, box_type_map_))
                {
                    return false;
                }

                if (!check_support(pos, single, sim, box_type_map_, problem_.support_rate))
                {
                    return false;
                }

                if (need_route)
                {
                    if (!check_route_order(
                            sim, block.platform, pos, single, problem_.route.value()))
                    {
                        return false;
                    }
                }

                Placement pl;
                pl.box_id = "simulated";
                pl.box_type_id = block.box_type_id;
                pl.position = pos;
                pl.orientation = block.orientation;
                pl.platform = block.platform;
                pl.group = block.group;
                sim.placements.push_back(std::move(pl));
            }
        }
    }

    return true;
}

void Heuristic::place_block(
    const SimpleBlock& block, const Space& space,
    ContainerLoad& state,
    std::map<std::string, std::vector<double>>& available,
    std::vector<Space>& stack) const
{
    auto single = box_type_map_.at(block.box_type_id).size.orient(block.orientation);

    auto& weights = available[block.box_type_id];
    double weight_sum = 0.0;
    for (int i = 0; i < block.box_count; ++i)
    {
        weight_sum += weights.back();
        weights.pop_back();
    }
    if (weights.empty())
    {
        available.erase(block.box_type_id);
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
                pl.platform = block.platform;
                pl.group = block.group;

                state.placements.push_back(std::move(pl));
                state.used_volume += single.volume();

                if (!block.platform.empty())
                {
                    state.platforms.insert(block.platform);
                    int32_t box_max_x = pos.x + single.dx;
                    auto xmax_it = state.platform_x_max.find(block.platform);
                    if (xmax_it == state.platform_x_max.end() || box_max_x > xmax_it->second)
                    {
                        state.platform_x_max[block.platform] = box_max_x;
                    }
                    int32_t box_min_x = pos.x;
                    auto xmin_it = state.platform_x_min.find(block.platform);
                    if (xmin_it == state.platform_x_min.end() || box_min_x < xmin_it->second)
                    {
                        state.platform_x_min[block.platform] = box_min_x;
                    }
                }

                if (!block.group.empty())
                {
                    state.groups.insert(block.group);
                }
            }
        }
    }

    state.total_weight += weight_sum;

    split_space(space, block.osize, stack);
}

// ===== Beam 搜索 =====

PackResult Heuristic::make_result(
    const ContainerLoad& state,
    const std::map<std::string, std::vector<double>>& /*available*/,
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

Heuristic::LocalPackScore Heuristic::score_state(const ContainerLoad& state) const
{
    LocalPackScore score;
    score.platform_split = static_cast<int>(state.platforms.size());
    score.group_count = static_cast<int>(state.groups.size());
    score.used_volume = state.used_volume;
    score.placed_count = static_cast<int>(state.placements.size());
    return score;
}

int Heuristic::compare_local_scores(const LocalPackScore& a,
                                    const LocalPackScore& b) const
{
    const auto& keys = problem_.objective_keys.empty()
                           ? default_objective_keys()
                           : problem_.objective_keys;

    for (const auto& key : keys)
    {
        if (key == "min_platform_split")
        {
            if (a.platform_split < b.platform_split)
            {
                return -1;
            }
            if (a.platform_split > b.platform_split)
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
    if (a.platform_split < b.platform_split)
    {
        return -1;
    }
    if (a.platform_split > b.platform_split)
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

const SimpleBlock* Heuristic::pick_best_block(
    const std::vector<const SimpleBlock*>& viable,
    const Space& space,
    const ContainerLoad& state,
    const std::map<std::string, std::vector<double>>& available,
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

PackResult Heuristic::pack_beam(const std::vector<Box>& boxes, int width)
{
    // 库存按 box_type_id 聚合（热路径用）
    std::map<std::string, std::vector<double>> all_available;
    for (const auto& bx : boxes)
    {
        all_available[bx.box_type_id].push_back(
            bx.weight.has_value() ? bx.weight.value() : 0.0);
    }

    // 块按 (type, platform, group) 分组生成（确保同平台同分组）
    std::map<std::string, int> group_counts;
    for (const auto& bx : boxes)
    {
        group_counts[bx.box_type_id + "\t" + bx.platform + "\t" + bx.group]++;
    }

    std::vector<SimpleBlock> all_blocks;
    for (const auto& [key, count] : group_counts)
    {
        auto tab1 = key.find('\t');
        auto tab2 = key.find('\t', tab1 + 1);
        auto tid = key.substr(0, tab1);
        auto plat = key.substr(tab1 + 1, tab2 - tab1 - 1);
        auto grp = key.substr(tab2 + 1);
        auto type_blocks = block_gen_.generate_for_type(tid, container_.inner_size, plat, grp, count);
        all_blocks.insert(all_blocks.end(), type_blocks.begin(), type_blocks.end());
    }

    sort_blocks_by_volume_desc(all_blocks);

    // 自适应：当块平均大小很小时（多箱型少数量场景），降低 beam 搜索精度
    int64_t total_box_count = 0;
    for (const auto& b : all_blocks)
    {
        total_box_count += b.box_count;
    }
    double avg_block_size = all_blocks.empty()
                                ? 1.0
                                : static_cast<double>(total_box_count) / all_blocks.size();
    bool tiny_blocks = (avg_block_size <= 2.0);

    const int kKeepTopN = tiny_blocks ? std::max(1, std::min(width, 4))
                                      : std::max(1, std::min(width, 16));
    const int kMaxRefineRounds = tiny_blocks ? 2 : 6;
    const int kMaxEvalWidth = tiny_blocks ? 6 : 4;

    ContainerLoad state;
    state.type = &container_;
    state.type_id = container_.id;

    auto available = all_available;
    std::vector<Space> stack;
    stack.push_back({{0, 0, 0}, container_.inner_size.x, container_.inner_size.y, container_.inner_size.z});

    while (!stack.empty() && !available.empty() && !all_blocks.empty())
    {
        if (!TimeChecker::check())
        {
            break;
        }

        all_blocks.erase(
            std::remove_if(all_blocks.begin(), all_blocks.end(),
                           [&](const SimpleBlock& b)
                           {
                               auto it = available.find(b.box_type_id);
                               return it == available.end() || it->second.size() < static_cast<size_t>(b.box_count);
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

        int eval_limit = tiny_blocks
                             ? std::min(static_cast<int>(viable.size()),
                                        std::max(width, kKeepTopN * 2))
                             : std::min(static_cast<int>(viable.size()),
                                        std::max(width, kKeepTopN * 4));
        std::vector<const SimpleBlock*> candidates(viable.begin(), viable.begin() + eval_limit);

        for (int round = 0; round < kMaxRefineRounds && candidates.size() > 1; ++round)
        {
            if (!TimeChecker::check())
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

                LocalPackScore fitness = tiny_blocks
                                             ? complete_largest(std::move(sim), std::move(sim_avail),
                                                                std::move(sim_stack), all_blocks)
                                             : greedy_complete(std::move(sim), std::move(sim_avail),
                                                               std::move(sim_stack), all_blocks, true);
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

    // 回填真实 box_id：按 (type, platform, group) 分组分配
    std::map<std::string, std::vector<std::string>> real_ids_by_group;
    for (const auto& bx : boxes)
    {
        real_ids_by_group[bx.box_type_id + "\t" + bx.platform + "\t" + bx.group].push_back(bx.id);
    }

    std::map<std::string, size_t> consume_idx;
    for (auto& pl : state.placements)
    {
        if (pl.box_id.compare(0, 8, "__block_") == 0)
        {
            auto key = pl.box_type_id + "\t" + pl.platform + "\t" + pl.group;
            auto& ids = real_ids_by_group[key];
            size_t& idx = consume_idx[key];
            if (idx < ids.size())
            {
                pl.box_id = ids[idx++];
            }
        }
    }

    return make_result(state, available, boxes);
}

Heuristic::LocalPackScore Heuristic::greedy_complete(
    ContainerLoad state,
    std::map<std::string, std::vector<double>> available,
    std::vector<Space> stack,
    const std::vector<SimpleBlock>& all_blocks,
    bool use_pick_best) const
{
    while (!stack.empty())
    {
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

        const SimpleBlock* best = nullptr;
        if (use_pick_best)
        {
            const int kEvalWidth = 4;
            int eval_count = std::min(kEvalWidth, static_cast<int>(viable.size()));
            best = pick_best_block(
                viable, space, state, available, stack, all_blocks, eval_count);
        }
        else
        {
            best = viable[0];
        }

        if (best == nullptr)
        {
            break;
        }

        place_block(*best, space, state, available, stack);
    }

    return score_state(state);
}

Heuristic::LocalPackScore Heuristic::complete_largest(
    ContainerLoad state,
    std::map<std::string, std::vector<double>> available,
    std::vector<Space> stack,
    const std::vector<SimpleBlock>& all_blocks) const
{
    while (!stack.empty())
    {
        Space space = stack.back();
        stack.pop_back();

        auto viable = filter_viable_blocks(all_blocks, space, available, state);

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
    }

    return score_state(state);
}

} // namespace hypercube::mlhs
