#include "heuristic.hpp"

#include <algorithm>
#include <cassert>
#include <map>
#include <set>

#include "../../constraints.hpp"
#include "../../objectives.hpp"
#include "../../packer_base.hpp"
#include "../../tool.hpp"
#include "../config.hpp"
#include "block.hpp"
#include "space.hpp"

namespace pack3d::glc
{

Heuristic::Heuristic(
    const ContainerType& container,
    const std::map<std::string, BoxType>& box_type_map,
    const std::map<std::string, Box>& box_map,
    const Problem& problem,
    bool has_weight_info,
    const TenderState& tender)
    : container_(container)
    , box_type_map_(box_type_map)
    , box_map_(box_map)
    , problem_(problem)
    , has_weight_info_(has_weight_info)
    , tender_(tender)
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

    // 利用 dx_index_ 仅扫描 dx ≤ space.lx 的块（避免 O(B) 全量扫描）
    auto end_it = std::upper_bound(
        dx_index_.begin(), dx_index_.end(), space.lx,
        [](int32_t lx, const std::pair<int32_t, size_t>& p) noexcept
        {
            return lx < p.first;
        });

    for (auto it = dx_index_.begin(); it != end_it; ++it)
    {
        const SimpleBlock& block = all_blocks[it->second];

        // 其余两维的快速尺寸检查
        if (block.osize.dy > space.ly || block.osize.dz > space.lz)
        {
            continue;
        }

        // 库存检查（按 type+platform+group 精确计数）
        auto avail_it = available.find(block.key);
        if (avail_it == available.end() ||
            avail_it->second.size() < static_cast<size_t>(block.box_count))
        {
            continue;
        }

        // 约束检查
        if (!check_block_feasible(block, space, state, available))
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
    const ContainerLoad& state,
    const std::map<std::string, std::vector<double>>& available) const
{
    auto single = box_type_map_.at(block.box_type_id).size.orient(block.orientation);

    // ---- per-block 检查 ----

    // tender 约束：块内同组，只查一次
    if (!check_tender_limit(tender_, state.groups, block.group))
    {
        return false;
    }

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

    // ---- 逐箱检查：边界、重叠、支撑、路线 ----
    ContainerLoad sim = state;
    if (!block.platform.empty())
    {
        sim.platforms.insert(block.platform);
    }

    // 该块消耗的精确单箱重量（队首 box_count 件，与 place_block / 末尾回填一致）
    const auto* w = &available.find(block.key)->second;

    int cell = 0;
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

                // 障碍物相交检查（空间雕刻的兜底安全网）
                if (check_obstacle(pos, single, container_.obstacles))
                {
                    return false;
                }

                // 斜面禁区检查（阶梯雕刻的兜底安全网）
                if (check_facet(pos, single, container_.inner_size, container_.facets))
                {
                    return false;
                }

                // 重叠检查仅需查 state.placements（块内箱子网格排列，互不重叠）
                if (check_overlap(pos, single, state.placements))
                {
                    return false;
                }

                if (!check_support(pos, single, sim, problem_.support_rate))
                {
                    return false;
                }

                double box_weight =
                    (cell < static_cast<int>(w->size())) ? (*w)[static_cast<size_t>(cell)] : 0.0;
                ++cell;
                if ((problem_.has_max_stack || problem_.has_max_load) &&
                    !check_stack_constraints(pos, single, box_weight, sim, box_type_map_))
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
                pl.osize = single;
                pl.platform = block.platform;
                pl.group = block.group;
                if (has_weight_info_ && problem_.has_max_load)
                {
                    pl.weight = box_weight;
                }
                sim.placements.push_back(std::move(pl));
                if (problem_.has_max_stack || problem_.has_max_load)
                {
                    apply_stack_state(pos, single, box_weight, sim);
                }
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

    // 该块消耗的精确单箱重量（队首 box_count 件，与 check_block_feasible / 末尾回填一致）
    auto& avail = available[block.key];
    double weight_sum = 0.0;
    for (int i = 0; i < block.box_count && static_cast<size_t>(i) < avail.size(); ++i)
    {
        weight_sum += avail[static_cast<size_t>(i)];
    }

    int placed = 0;
    int cell = 0;
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

                double box_weight =
                    (cell < block.box_count && static_cast<size_t>(cell) < avail.size())
                        ? avail[static_cast<size_t>(cell)]
                        : 0.0;
                ++cell;

                Placement pl;
                pl.box_id = std::string("__block_") + std::to_string(placed++);
                pl.box_type_id = block.box_type_id;
                pl.position = pos;
                pl.orientation = block.orientation;
                pl.osize = single;
                pl.platform = block.platform;
                pl.group = block.group;
                if (has_weight_info_ && problem_.has_max_load)
                {
                    pl.weight = box_weight;
                }

                state.placements.push_back(std::move(pl));
                if (problem_.has_max_stack || problem_.has_max_load)
                {
                    apply_stack_state(pos, single, box_weight, state);
                }
                state.used_volume += single.volume();

                if (!block.platform.empty())
                {
                    state.platforms.insert(block.platform);
                }

                if (!block.group.empty())
                {
                    state.groups.insert(block.group);
                }
            }
        }
    }

    // 消耗队首 box_count 件
    avail.erase(avail.begin(), avail.begin() + static_cast<ptrdiff_t>(block.box_count));
    if (avail.empty())
    {
        available.erase(block.key);
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
    if (a.platform_split != b.platform_split)
    {
        return a.platform_split < b.platform_split ? -1 : 1;
    }
    if (a.used_volume != b.used_volume)
    {
        return a.used_volume > b.used_volume ? -1 : 1;
    }
    if (a.group_count != b.group_count)
    {
        return a.group_count < b.group_count ? -1 : 1;
    }
    if (a.placed_count != b.placed_count)
    {
        return a.placed_count > b.placed_count ? -1 : 1;
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

std::vector<Space> Heuristic::reconstruct_spaces(
    const std::vector<Placement>& existing,
    const Size& container_size)
{
    std::vector<Space> stack;
    stack.push_back({{0, 0, 0}, container_size.x, container_size.y, container_size.z});

    // 按 z asc, y asc, x asc 排序，接近 GLC 自然放置顺序
    auto sorted = existing;
    std::sort(sorted.begin(), sorted.end(),
              [](const Placement& a, const Placement& b)
              {
                  if (a.position.z != b.position.z)
                      return a.position.z < b.position.z;
                  if (a.position.y != b.position.y)
                      return a.position.y < b.position.y;
                  return a.position.x < b.position.x;
              });

    for (const auto& pl : sorted)
    {
        bool found = false;

        // 尝试匹配 Space 角落（从栈顶找，栈顶优先处理）
        for (int i = static_cast<int>(stack.size()) - 1; i >= 0; --i)
        {
            const auto& sp = stack[static_cast<size_t>(i)];
            if (sp.pos.x == pl.position.x &&
                sp.pos.y == pl.position.y &&
                sp.pos.z == pl.position.z &&
                sp.lx >= pl.osize.dx && sp.ly >= pl.osize.dy && sp.lz >= pl.osize.dz)
            {
                auto space = sp;
                stack.erase(stack.begin() + i);
                split_space(space, pl.osize, stack);
                found = true;
                break;
            }
        }

        if (found)
        {
            continue;
        }

        // 兜底：不在任何 Space 角落 → 6 向切割
        for (int i = static_cast<int>(stack.size()) - 1; i >= 0; --i)
        {
            const auto& sp = stack[static_cast<size_t>(i)];
            if (pl.position.x >= sp.pos.x &&
                pl.position.y >= sp.pos.y &&
                pl.position.z >= sp.pos.z &&
                pl.position.x + pl.osize.dx <= sp.pos.x + sp.lx &&
                pl.position.y + pl.osize.dy <= sp.pos.y + sp.ly &&
                pl.position.z + pl.osize.dz <= sp.pos.z + sp.lz)
            {
                auto space = sp;
                stack.erase(stack.begin() + i);
                carve_out_space(space, pl, stack);
                found = true;
                break;
            }
        }
    }

    return stack;
}

PackResult Heuristic::pack_beam(const std::vector<Box>& boxes,
                                const std::vector<Placement>& existing,
                                int width)
{
    // 库存按 (type, platform, group) 聚合（与块/回填分组一致，热路径用），重量为精确单箱重量
    std::map<std::string, std::vector<double>> all_available;
    for (const auto& bx : boxes)
    {
        all_available[bx.box_type_id + "\t" + bx.platform + "\t" + bx.group].push_back(
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

    // 构建 dx 维度索引：按块 dx 升序排列，避免 filter_viable_blocks 中 O(B) 全量扫描
    dx_index_.clear();
    dx_index_.reserve(all_blocks.size());
    for (size_t i = 0; i < all_blocks.size(); ++i)
    {
        dx_index_.emplace_back(all_blocks[i].osize.dx, i);
    }
    std::sort(dx_index_.begin(), dx_index_.end());

    // 自适应：当块平均大小很小时（多箱型少数量场景），降低 beam 搜索精度
    int64_t total_box_count = 0;
    for (const auto& b : all_blocks)
    {
        total_box_count += b.box_count;
    }
    double avg_block_size = all_blocks.empty()
                                ? 1.0
                                : static_cast<double>(total_box_count) / all_blocks.size();

    const int kKeepTopN = std::max(1, std::min(width, config::GLC_KEEP_TOP_N_CAP));
    const int kMaxRefineRounds = config::GLC_MAX_REFINE_ROUNDS;

    ContainerLoad state;
    state.type = &container_;
    state.type_id = container_.id;

    auto available = all_available;

    // 预填充已有放置
    prefill_load(state, existing, box_map_);

    std::vector<Space> stack;
    if (existing.empty())
    {
        stack.push_back({{0, 0, 0}, container_.inner_size.x, container_.inner_size.y, container_.inner_size.z});
    }
    else
    {
        stack = reconstruct_spaces(existing, container_.inner_size);
    }

    // 初始空间 = 容器减障碍物（凸自由空间分解）
    carve_obstacles(stack, container_.obstacles);
    // 再挖掉斜面楔形禁区（阶梯近似）
    carve_facets(stack, container_.inner_size, container_.facets);

    while (!stack.empty() && !available.empty())
    {
        if (!TimeChecker::check())
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

        // 自适应候选数：块包含箱子越多（大块），候选数越多
        // avg_block_size 小时（多小块场景）取 width 下限以控制开销
        int eval_limit = std::min(static_cast<int>(viable.size()),
                                  std::max(width, static_cast<int>(kKeepTopN * avg_block_size)));
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

                LocalPackScore fitness = greedy_complete(
                    std::move(sim), std::move(sim_avail), std::move(sim_stack), all_blocks, true);
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
            LocalPackScore new_score = greedy_complete(
                std::move(sim), std::move(sim_avail), std::move(sim_stack), all_blocks, true);
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
                auto bx_it = box_map_.find(pl.box_id);
                if (bx_it != box_map_.end())
                {
                    pl.weight = bx_it->second.weight;
                }
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
        if (!TimeChecker::check())
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

        const SimpleBlock* best = nullptr;
        if (use_pick_best)
        {
            const int eval_count = std::min(config::GLC_EVAL_WIDTH, static_cast<int>(viable.size()));
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
        if (!TimeChecker::check())
        {
            break;
        }

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

} // namespace pack3d::glc
