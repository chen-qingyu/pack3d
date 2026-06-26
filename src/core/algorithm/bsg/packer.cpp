#include "packer.hpp"

#include <algorithm>
#include <cassert>

#include <spdlog/spdlog.h>

#include "../../constraints.hpp"
#include "../../tool.hpp"
#include "../config.hpp"
#include "block.hpp"
#include "solver.hpp"

namespace hypercube::bsg
{

Solution pack(const Problem& problem,
              const std::map<std::string, BoxType>& box_type_map,
              const std::map<std::string, Box>& box_map)
{
    Solution sol;
    sol.status = SolveStatus::Complete;
    sol.box_types = problem.box_types;

    // 选最大容器
    if (problem.container_types.empty())
    {
        sol.status = SolveStatus::Invalid;
        return sol;
    }

    const ContainerType* best_ct = &problem.container_types[0];
    int64_t best_vol = best_ct->inner_size.volume();
    for (const auto& ct : problem.container_types)
    {
        int64_t v = ct.inner_size.volume();
        if (v > best_vol)
        {
            best_vol = v;
            best_ct = &ct;
        }
    }

    // 构建 BoxType 索引表（BSG 内部用整数索引）
    std::vector<BoxType> box_types;
    std::map<std::string, int> type_idx_map;
    for (const auto& bt : problem.box_types)
    {
        type_idx_map[bt.id] = static_cast<int>(box_types.size());
        box_types.push_back(bt);
    }

    int n_types = static_cast<int>(box_types.size());

    // 构建 per-type 箱子 ID 队列和初始件数
    std::vector<std::vector<std::string>> box_ids_by_type(n_types);
    std::vector<int> initial_counts(n_types, 0);

    for (const auto& bx : problem.boxes)
    {
        auto it = type_idx_map.find(bx.box_type_id);
        if (it == type_idx_map.end())
        {
            continue;
        }
        int ti = it->second;
        box_ids_by_type[ti].push_back(bx.id);
        initial_counts[ti]++;
    }

    // 块生成
    double max_fr = (n_types < 30) ? 1.00 : 0.98;
    spdlog::debug("BSG block gen: {} box types, max_fr={}, max_bl={}",
                  n_types, max_fr, config::BSG_MAX_BL);

    auto blocks = generate_blocks(best_ct->inner_size, box_types, initial_counts,
                                  max_fr, config::BSG_MAX_BL);
    spdlog::debug("BSG blocks generated: {}", blocks.size());

    // 构建全局上下文
    GlobalContext ctx;
    ctx.container_size = best_ct->inner_size;
    ctx.box_types = std::move(box_types);
    ctx.blocks = std::move(blocks);

    // 箱子 ID 列表
    for (const auto& bx : problem.boxes)
    {
        ctx.box_ids.push_back(bx.id);
    }

    // 求解
    PackResult pr = solve(ctx, initial_counts, box_ids_by_type, problem.time_limit);

    // 构建 Solution
    auto elapsed = TimeChecker::elapsed();
    sol.elapsed_second = elapsed;

    sol.packed_box_count = static_cast<int>(pr.placements.size());
    sol.unpacked_box_count = static_cast<int>(pr.unpacked_box_ids.size());
    sol.unpacked_boxes = pr.unpacked_box_ids;

    sol.objective.container_count = 1;
    int64_t container_vol = best_ct->inner_size.volume();
    sol.objective.avg_volume_rate = container_vol > 0
                                        ? static_cast<double>(pr.used_volume) / static_cast<double>(container_vol)
                                        : 0.0;
    sol.objective.platform_split = 0;
    sol.objective.group_split_sum = 0;

    // 容器摘要
    ContainerSummary cs;
    cs.type_id = best_ct->id;
    cs.inner_size = best_ct->inner_size;
    cs.max_weight = best_ct->max_weight;
    cs.used_volume = pr.used_volume;
    cs.volume_rate = sol.objective.avg_volume_rate;
    cs.packed_count = sol.packed_box_count;
    sol.container_summaries.push_back(std::move(cs));

    // 容器内放置
    for (auto& pl : pr.placements)
    {
        pl.container_id = best_ct->id;
    }
    sol.container_placements.push_back(std::move(pr.placements));

    // 判断状态
    if (sol.unpacked_box_count == 0)
    {
        sol.status = SolveStatus::Complete;
    }
    else if (!TimeChecker::check())
    {
        sol.status = SolveStatus::Timeout;
    }
    else
    {
        sol.status = SolveStatus::Partial;
    }

    spdlog::info("Container#1 \"{}\": packed {}, left {}, volume rate: {:.4f}",
                 best_ct->id, sol.packed_box_count, sol.unpacked_box_count,
                 sol.objective.avg_volume_rate);

    return sol;
}

} // namespace hypercube::bsg
