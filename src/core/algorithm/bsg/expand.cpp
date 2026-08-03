#include "expand.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <utility>

#include "feasibility.hpp"
#include "kpa.hpp"
#include "space.hpp"
#include "support.hpp"

namespace pack3d::bsg
{

namespace
{

// 过滤 available_blocks：移除依赖已耗尽箱型的块
void filter_available(std::vector<int>& avail,
                      const std::vector<int>& remaining_counts,
                      const std::vector<GeneralBlock>& blocks)
{
    size_t w = 0;
    for (size_t i = 0; i < avail.size(); ++i)
    {
        int bi = avail[i];
        const auto& b = blocks[bi];
        bool ok = true;
        for (const auto& m : b.members)
        {
            if (m.type_idx < 0 || static_cast<size_t>(m.type_idx) >= remaining_counts.size())
            {
                ok = false;
                break;
            }
            if (m.count > remaining_counts[m.type_idx])
            {
                ok = false;
                break;
            }
        }
        if (ok)
        {
            if (w != i)
            {
                avail[w] = avail[i];
            }
            ++w;
        }
    }
    avail.resize(w);
}

} // namespace

std::vector<BSGState> expand(
    const BSGState& s,
    int w,
    const GlobalContext& ctx)
{
    std::vector<BSGState> successors;

    if (s.R.empty() || s.available_blocks.empty())
    {
        return successors;
    }

    SpaceSelection selection = select_free_space(s.R, ctx.container_size);
    const Cuboid& r = s.R[selection.cuboid_index];

    // K4: 对每个可用块计算 f(b, r)，取 top-w
    // 确保 KPA 已计算
    assert(s.kpa_L.has_value() && s.kpa_W.has_value() && s.kpa_H.has_value());

    struct Candidate
    {
        int block_idx;
        int64_t f_value;
    };
    std::vector<Candidate> candidates;

    for (int bi : s.available_blocks)
    {
        const auto& b = ctx.blocks[bi];

        // 尺寸检查
        if (b.osize.dx > r.lx || b.osize.dy > r.ly || b.osize.dz > r.lz)
        {
            continue;
        }

        // 库存检查
        bool avail = true;
        for (const auto& m : b.members)
        {
            if (m.count > s.remaining_counts[m.type_idx])
            {
                avail = false;
                break;
            }
        }
        if (!avail)
        {
            continue;
        }

        int64_t fv = compute_f(s, r, b, ctx);
        if (fv == std::numeric_limits<int64_t>::lowest())
        {
            continue;
        }
        candidates.push_back({bi, fv});
    }

    if (candidates.empty())
    {
        return successors;
    }

    // 按 f 降序，取 top-w；并列按 block_idx 升序，保证跨平台确定性
    int take = std::min(w, static_cast<int>(candidates.size()));
    std::partial_sort(candidates.begin(),
                      candidates.begin() + take,
                      candidates.end(),
                      [](const Candidate& a, const Candidate& b) noexcept
                      {
                          if (a.f_value != b.f_value)
                          {
                              return a.f_value > b.f_value;
                          }
                          return a.block_idx < b.block_idx;
                      });

    // 为评分最高的可行块生成后继状态。
    for (size_t i = 0; i < candidates.size() && static_cast<int>(successors.size()) < take; ++i)
    {
        int bi = candidates[i].block_idx;
        const auto& b = ctx.blocks[bi];

        // K5: 计算放置位置
        Position place_pos = placement_position(r, b, selection.anchor);
        if (!ctx.needs_leaf_validation() && !is_supported(s, place_pos, b.osize, ctx))
        {
            continue;
        }

        BSGState succ = s; // copy
        if (ctx.needs_leaf_validation() &&
            !can_place_block(s, bi, place_pos, ctx,
                             succ.constraint_load, succ.item_class_indices))
        {
            continue;
        }

        // 更新残差空间
        update_residual_space(succ.R, place_pos, b.osize);

        // 扣除库存
        for (const auto& m : b.members)
        {
            succ.remaining_counts[m.type_idx] -= m.count;
        }

        // 过滤可用块
        filter_available(succ.available_blocks, succ.remaining_counts, ctx.blocks);

        // 更新已用体积和放置记录
        succ.used_volume += b.single_box_volume;
        succ.placements.push_back({bi, place_pos});

        // KPA 缓存失效（库存变了）
        succ.kpa_L.reset();
        succ.kpa_W.reset();
        succ.kpa_H.reset();

        successors.push_back(std::move(succ));
    }

    return successors;
}

} // namespace pack3d::bsg
