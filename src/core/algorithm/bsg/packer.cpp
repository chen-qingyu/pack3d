#include "packer.hpp"

#include <algorithm>
#include <cassert>
#include <set>

#include <spdlog/spdlog.h>

#include "../../constraints.hpp"
#include "../../tool.hpp"
#include "../config.hpp"
#include "block.hpp"
#include "solver.hpp"

namespace pack3d::bsg
{

Solution pack(const Problem& problem,
              const std::map<std::string, BoxType>& box_type_map,
              const std::map<std::string, Box>& box_map)
{
    Solution sol;
    sol.status = SolveStatus::Complete;
    sol.box_types = problem.box_types;

    // BoxType 索引表（全局不变）
    std::vector<BoxType> box_types;
    std::map<std::string, int> type_idx_map;
    for (const auto& bt : problem.box_types)
    {
        type_idx_map[bt.id] = static_cast<int>(box_types.size());
        box_types.push_back(bt);
    }
    int n_types = static_cast<int>(box_types.size());

    // 剩余箱子 ID 集合
    std::set<std::string> remaining_ids;
    for (const auto& bx : problem.boxes)
    {
        remaining_ids.insert(bx.id);
    }

    std::map<std::string, int> container_usage;
    int container_num = 0;
    double sum_volume_rate = 0.0;

    while (!remaining_ids.empty() && TimeChecker::check())
    {
        // 选最大容器（考虑数量限制）
        const ContainerType* best_ct = nullptr;
        int64_t best_vol = -1;
        for (const auto& ct : problem.container_types)
        {
            if (!ct.has_remaining(container_usage))
            {
                continue;
            }
            int64_t v = ct.inner_size.volume();
            if (v > best_vol)
            {
                best_vol = v;
                best_ct = &ct;
            }
        }
        if (best_ct == nullptr)
        {
            break;
        }

        // 统计当前剩余箱子的 per-type 数量和 ID 列表
        std::vector<int> cur_counts(n_types, 0);
        std::vector<std::vector<std::string>> cur_ids_by_type(n_types);
        for (const auto& id : remaining_ids)
        {
            const auto& bx = box_map.at(id);
            auto it = type_idx_map.find(bx.box_type_id);
            if (it == type_idx_map.end())
            {
                continue;
            }
            int ti = it->second;
            cur_counts[ti]++;
            cur_ids_by_type[ti].push_back(id);
        }

        // 块生成
        double max_fr = (n_types < 30) ? 1.00 : 0.98;
        auto blocks = generate_blocks(best_ct->inner_size, box_types, cur_counts,
                                      max_fr, config::BSG_MAX_BL);

        GlobalContext ctx;
        ctx.container_size = best_ct->inner_size;
        ctx.box_types = box_types;
        ctx.blocks = std::move(blocks);

        // 求解
        PackResult pr = solve(ctx, cur_counts, cur_ids_by_type, problem.time_limit);

        // 记录容器
        ++container_num;
        container_usage[best_ct->id]++;

        int64_t container_vol = best_ct->inner_size.volume();
        double vol_rate = container_vol > 0
                              ? static_cast<double>(pr.used_volume) / static_cast<double>(container_vol)
                              : 0.0;

        int left = static_cast<int>(remaining_ids.size()) - static_cast<int>(pr.placements.size());
        spdlog::info("Container#{} \"{}\": packed {}, left {}, volume rate: {:.4f}",
                     container_num, best_ct->id, pr.placements.size(), left, vol_rate);

        // 从剩余集合中移除已装箱
        for (const auto& pl : pr.placements)
        {
            remaining_ids.erase(pl.box_id);
        }

        if (pr.placements.empty())
        {
            break;
        }

        // 容器摘要与放置
        ContainerSummary cs;
        cs.type_id = best_ct->id;
        cs.inner_size = best_ct->inner_size;
        cs.max_weight = best_ct->max_weight;
        cs.used_volume = pr.used_volume;
        cs.volume_rate = vol_rate;
        cs.packed_count = static_cast<int>(pr.placements.size());
        sol.container_summaries.push_back(std::move(cs));

        for (auto& pl : pr.placements)
        {
            pl.container_id = best_ct->id;
        }
        sol.container_placements.push_back(std::move(pr.placements));

        sum_volume_rate += vol_rate;
    }

    // 汇总
    int total_packed = 0;
    for (const auto& cs : sol.container_summaries)
    {
        total_packed += cs.packed_count;
    }
    sol.packed_box_count = total_packed;
    sol.unpacked_box_count = static_cast<int>(remaining_ids.size());
    for (const auto& id : remaining_ids)
    {
        sol.unpacked_boxes.push_back(id);
    }

    int container_count = static_cast<int>(sol.container_summaries.size());
    sol.objective.container_count = container_count;
    sol.objective.avg_volume_rate = container_count > 0 ? sum_volume_rate / container_count : 0.0;
    sol.objective.platform_split = 0;
    sol.objective.group_split_sum = 0;

    if (remaining_ids.empty())
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

    sol.elapsed_second = TimeChecker::elapsed();
    return sol;
}

} // namespace pack3d::bsg
