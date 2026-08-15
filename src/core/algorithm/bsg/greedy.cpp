#include "greedy.hpp"

#include <algorithm>
#include <limits>
#include <set>

#include "feasibility.hpp"
#include "kpa.hpp"
#include "space.hpp"
#include "support.hpp"

namespace pack3d::bsg
{

int count_remaining_platforms(const BSGState& state, const GlobalContext& ctx)
{
    if (ctx.item_classes.empty())
    {
        return 0;
    }
    std::set<std::string> platforms;
    for (size_t ti = 0; ti < state.remaining_counts.size() && ti < ctx.item_classes.size(); ++ti)
    {
        if (state.remaining_counts[ti] > 0 && !ctx.item_classes[ti].platform.empty())
        {
            platforms.insert(ctx.item_classes[ti].platform);
        }
    }
    return static_cast<int>(platforms.size());
}

int64_t max_remaining_volume(const BSGState& state, const GlobalContext& ctx)
{
    int64_t total = 0;
    for (size_t ti = 0; ti < ctx.box_types.size(); ++ti)
    {
        if (ti >= state.remaining_counts.size())
        {
            continue;
        }
        int count = state.remaining_counts[ti];
        if (count <= 0)
        {
            continue;
        }
        total += static_cast<int64_t>(ctx.box_types[ti].size.volume()) * count;
    }
    return total;
}

GreedyResult greedy_rollout(
    const BSGState& state,
    int64_t s_best_volume,
    const GlobalContext& ctx)
{
    BSGState cur = state; // copy

    // 初始 KPA
    run_kpa(cur, ctx);

    // 初始已装箱数（拷贝父状态的）
    int n_types = static_cast<int>(ctx.box_types.size());
    std::vector<int> packed_counts(n_types, 0);

    // 从已有放置恢复 packed_counts
    for (const auto& pl : cur.placements)
    {
        const auto& b = ctx.blocks[pl.block_idx];
        for (const auto& m : b.members)
        {
            packed_counts[m.type_idx] += m.count;
        }
    }

    while (true)
    {
        // 无可用空间或无可用块 → 停止
        if (cur.R.empty() || cur.available_blocks.empty())
        {
            break;
        }

        // 剪枝：当前体积 + 剩余上界 ≤ 最优 → 不可能超越
        if (s_best_volume > 0)
        {
            int64_t upper = cur.used_volume + max_remaining_volume(cur, ctx);
            if (upper <= s_best_volume)
            {
                break;
            }
        }

        SpaceSelection selection = select_free_space(cur.R, ctx.container_size);
        const Cuboid& r = cur.R[selection.cuboid_index];

        // K4: 选 f(b, r) 最大的块；约束模式下候选循环已逐叶校验，胜出块的叶子状态随选中一起
        // 保存，提交时直接复用，避免二次 can_place_block
        int best_bi = -1;
        int64_t best_f = std::numeric_limits<int64_t>::lowest();
        ContainerLoad best_load;
        std::vector<int> best_classes;

        for (int bi : cur.available_blocks)
        {
            const auto& b = ctx.blocks[bi];
            if (b.osize.dx > r.lx || b.osize.dy > r.ly || b.osize.dz > r.lz)
            {
                continue;
            }
            bool avail = true;
            for (const auto& m : b.members)
            {
                if (m.count > cur.remaining_counts[m.type_idx])
                {
                    avail = false;
                    break;
                }
            }
            if (!avail)
            {
                continue;
            }

            Position place_pos = placement_position(r, b, selection.anchor);
            ContainerLoad next_load;
            std::vector<int> next_classes;
            if (ctx.needs_leaf_validation())
            {
                if (!can_place_block(cur, bi, place_pos, ctx, next_load, next_classes))
                {
                    continue;
                }
            }
            else if (!is_supported(cur, place_pos, b.osize, ctx))
            {
                continue;
            }

            int64_t fv = compute_f(cur, r, b, ctx);
            if (fv > best_f)
            {
                best_f = fv;
                best_bi = bi;
                if (ctx.needs_leaf_validation())
                {
                    best_load = std::move(next_load);
                    best_classes = std::move(next_classes);
                }
            }
        }

        if (best_bi < 0)
        {
            break;
        } // 无可放置的块

        // 放置
        const auto& b = ctx.blocks[best_bi];
        Position place_pos = placement_position(r, b, selection.anchor);

        if (ctx.needs_leaf_validation())
        {
            // best_bi 只在 can_place_block 通过时更新，此处必可成功，直接复用候选循环的校验结果
            cur.constraint_load = std::move(best_load);
            cur.item_class_indices = std::move(best_classes);
        }

        update_residual_space(cur.R, place_pos, b.osize);

        for (const auto& m : b.members)
        {
            cur.remaining_counts[m.type_idx] -= m.count;
            packed_counts[m.type_idx] += m.count;
        }

        cur.used_volume += b.single_box_volume;
        cur.placements.push_back({best_bi, place_pos});

        // 过滤可用块
        {
            size_t w = 0;
            for (size_t i = 0; i < cur.available_blocks.size(); ++i)
            {
                int bi2 = cur.available_blocks[i];
                const auto& b2 = ctx.blocks[bi2];
                bool ok = true;
                for (const auto& m : b2.members)
                {
                    if (m.count > cur.remaining_counts[m.type_idx])
                    {
                        ok = false;
                        break;
                    }
                }
                if (ok)
                {
                    if (w != i)
                    {
                        cur.available_blocks[w] = cur.available_blocks[i];
                    }
                    ++w;
                }
            }
            cur.available_blocks.resize(w);
        }

        // 不重算 KPA：论文 "once per state"，初始 KPA 复用至 greedy 结束
    }

    int remaining_platforms = count_remaining_platforms(cur, ctx);
    return {cur.used_volume, remaining_platforms, std::move(packed_counts), std::move(cur)};
}

} // namespace pack3d::bsg
