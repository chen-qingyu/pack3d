#include "solver.hpp"

#include <cassert>
#include <cmath>
#include <queue>
#include <set>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include "../../tool.hpp"
#include "beam.hpp"
#include "block.hpp"
#include "kpa.hpp"

namespace hypercube::bsg
{

namespace
{

// 递归展开 GeneralBlock 为独立的箱子 Placement
// base_pos: 此 block 在容器中的绝对位置（min corner）
// id_to_idx: 从 block id 到数组索引的映射
void expand_block_placements(
    int block_idx,
    Position base_pos,
    const std::vector<GeneralBlock>& blocks,
    const std::unordered_map<int64_t, int>& id_to_idx,
    const std::vector<BoxType>& box_types,
    std::vector<std::queue<std::string>>& id_queues, // per-type box ID queues
    std::vector<Placement>& out)
{
    const auto& b = blocks[block_idx];

    if (b.merge_axis == GeneralBlock::MergeAxis::None)
    {
        // 简单块：生成 nx×ny×nz 网格
        auto single = box_types[b.type_idx].size.orient(b.orientation);
        for (int iz = 0; iz < b.nz; ++iz)
        {
            for (int iy = 0; iy < b.ny; ++iy)
            {
                for (int ix = 0; ix < b.nx; ++ix)
                {
                    Placement pl;
                    pl.box_id = id_queues[b.type_idx].front();
                    id_queues[b.type_idx].pop();
                    pl.box_type_id = box_types[b.type_idx].id;
                    pl.position.x = base_pos.x + ix * single.dx;
                    pl.position.y = base_pos.y + iy * single.dy;
                    pl.position.z = base_pos.z + iz * single.dz;
                    pl.orientation = b.orientation;
                    pl.osize = single;
                    out.push_back(std::move(pl));
                }
            }
        }
    }
    else
    {
        // 合并块：递归展开左右子块
        int left_idx = id_to_idx.at(b.source_left_id);
        int right_idx = id_to_idx.at(b.source_right_id);
        const auto& left = blocks[left_idx];
        const auto& right = blocks[right_idx];

        Position left_pos = base_pos;
        Position right_pos = base_pos;

        switch (b.merge_axis)
        {
            case GeneralBlock::MergeAxis::X:
                right_pos.x = base_pos.x + left.osize.dx;
                break;
            case GeneralBlock::MergeAxis::Y:
                right_pos.y = base_pos.y + left.osize.dy;
                break;
            case GeneralBlock::MergeAxis::Z:
                right_pos.z = base_pos.z + left.osize.dz;
                break;
            default:
                assert(false && "Invalid merge axis");
                break;
        }

        expand_block_placements(left_idx, left_pos, blocks, id_to_idx, box_types, id_queues, out);
        expand_block_placements(right_idx, right_pos, blocks, id_to_idx, box_types, id_queues, out);
    }
}

} // namespace

PackResult solve(const GlobalContext& ctx,
                 const std::vector<int>& initial_counts,
                 const std::vector<std::vector<std::string>>& box_ids_by_type,
                 double time_limit_sec)
{
    PackResult result;

    int n_types = static_cast<int>(ctx.box_types.size());

    // 构建初始状态
    BSGState s0;
    s0.R.push_back({{0, 0, 0}, ctx.container_size.x, ctx.container_size.y, ctx.container_size.z});
    s0.remaining_counts = initial_counts;
    s0.available_blocks.reserve(ctx.blocks.size());
    for (int i = 0; i < static_cast<int>(ctx.blocks.size()); ++i)
    {
        s0.available_blocks.push_back(i);
    }
    s0.used_volume = 0;

    TimeChecker::init(time_limit_sec);

    int w = 1;
    int64_t s_best_volume = 0;
    BSGState s_best;

    int total_boxes = 0;
    for (int c : initial_counts)
    {
        total_boxes += c;
    }
    int64_t total_box_volume = 0;
    for (size_t ti = 0; ti < ctx.box_types.size(); ++ti)
    {
        total_box_volume += static_cast<int64_t>(ctx.box_types[ti].size.volume()) * initial_counts[ti];
    }

    spdlog::debug("BSG-CLP start: {} boxes ({} types), {} blocks, container {}x{}x{}",
                  total_boxes, n_types, ctx.blocks.size(),
                  ctx.container_size.x, ctx.container_size.y, ctx.container_size.z);

    while (TimeChecker::check())
    {
        spdlog::debug("Beam round: w={}", w);
        int64_t round_best = beam_search(s0, w, s_best_volume, s_best, ctx);
        spdlog::debug("Beam round done: w={}, best={}, s_best_vol={}, placements={}",
                      w, round_best, s_best_volume, s_best.placements.size());

        // 已达理论上界 → 提前停止
        if (s_best_volume >= total_box_volume)
        {
            spdlog::debug("BSG-CLP: optimal reached ({} / {}), stopping early",
                          s_best_volume, total_box_volume);
            break;
        }

        // w 上限：不超过可用块数，也不超过合理最大值
        static constexpr int MAX_W = 1000;
        int max_w = std::min(static_cast<int>(ctx.blocks.size()), MAX_W);
        int next_w = static_cast<int>(std::ceil(std::sqrt(2.0) * w));
        if (next_w > max_w || next_w <= w)
        {
            spdlog::debug("BSG-CLP: w capped at {}", std::max(w, max_w));
            break;
        }
        w = next_w;
    }

    if (s_best_volume <= 0)
    {
        spdlog::debug("BSG-CLP: no solution found");
        return result;
    }

    // 将 s_best 的块展开为独立箱子
    std::vector<std::queue<std::string>> id_queues(n_types);
    for (int ti = 0; ti < n_types; ++ti)
    {
        for (const auto& id : box_ids_by_type[ti])
        {
            id_queues[ti].push(id);
        }
    }

    // 构建 id → index 映射
    std::unordered_map<int64_t, int> id_to_idx;
    for (int i = 0; i < static_cast<int>(ctx.blocks.size()); ++i)
    {
        id_to_idx[ctx.blocks[i].id] = i;
    }

    std::vector<Placement> placements;
    for (const auto& pb : s_best.placements)
    {
        expand_block_placements(pb.block_idx, pb.anchor, ctx.blocks, id_to_idx, ctx.box_types,
                                id_queues, placements);
    }

    // 收集未装箱 ID
    std::set<std::string> packed_ids;
    for (const auto& pl : placements)
    {
        packed_ids.insert(pl.box_id);
    }
    std::vector<std::string> unpacked;
    for (int ti = 0; ti < n_types; ++ti)
    {
        for (const auto& id : box_ids_by_type[ti])
        {
            if (!packed_ids.count(id))
            {
                unpacked.push_back(id);
            }
        }
    }

    int64_t container_vol = ctx.container_size.volume();
    result.success = unpacked.empty();
    result.placements = std::move(placements);
    result.unpacked_box_ids = std::move(unpacked);
    result.used_volume = s_best_volume;

    auto elapsed = TimeChecker::elapsed();
    spdlog::debug("BSG-CLP done: {:.2f}s, volume_rate={:.4f}, packed={}, unpacked={}",
                  elapsed,
                  container_vol > 0 ? static_cast<double>(s_best_volume) / static_cast<double>(container_vol) : 0.0,
                  result.placements.size(), result.unpacked_box_ids.size());

    return result;
}

} // namespace hypercube::bsg
