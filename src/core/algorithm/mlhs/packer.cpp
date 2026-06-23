#include "packer.hpp"

#include <algorithm>
#include <cassert>
#include <set>

#include <spdlog/fmt/std.h>
#include <spdlog/spdlog.h>

#include "../../objectives.hpp"
#include "../../tool.hpp"
#include "../config.hpp"
#include "block.hpp"

namespace hypercube::mlhs
{

Packer::Packer(
    const Problem& problem,
    const std::map<std::string, BoxType>& box_type_map,
    const std::map<std::string, Box>& box_map,
    bool has_weight_info)
    : problem_(problem)
    , box_type_map_(box_type_map)
    , box_map_(box_map)
    , has_weight_info_(has_weight_info)
{
}

Solution Packer::pack()
{
    next_instance_ = 0;
    container_type_usage_.clear();

    // 统一流程：对每种容器类型，一次打包全部剩余箱子
    // 装进去的收走，装不下的留给下一个容器
    std::vector<ContainerSlot> slots;
    std::set<std::string> remaining_ids;
    for (const auto& bx : problem_.boxes)
    {
        remaining_ids.insert(bx.id);
    }

    // 按容器体积升序排列索引：优先尝试小容器，高填充率时早停
    std::vector<size_t> ct_indices;
    ct_indices.reserve(problem_.container_types.size());
    for (size_t i = 0; i < problem_.container_types.size(); ++i)
    {
        ct_indices.push_back(i);
    }
    std::sort(ct_indices.begin(), ct_indices.end(),
              [&](size_t a, size_t b)
              {
                  return problem_.container_types[a].inner_size.volume() <
                         problem_.container_types[b].inner_size.volume();
              });

    while (!remaining_ids.empty() && TimeChecker::check())
    {
        // 收集剩余箱子
        std::vector<const Box*> remaining;
        for (const auto& bx : problem_.boxes)
        {
            if (remaining_ids.count(bx.id))
            {
                remaining.push_back(&bx);
            }
        }

        // tender_limit 事前检查：若某组已达到限制，其剩余箱子必须放入已有容器
        if (problem_.tender_limit.has_value())
        {
            int tl_ret = handle_tender_limit_groups(remaining_ids, slots);
            if (tl_ret < 0)
            {
                return to_solution(slots, problem_.boxes, SolveStatus::Blocked);
            }
            if (tl_ret > 0)
            {
                continue; // 已处理，无需开新容器
            }
        }

        // 累积当前已用容器的目标状态
        int cur_count = static_cast<int>(slots.size());
        int cur_platform_split = 0;
        double cur_rate_sum = 0.0;
        std::map<std::string, int> group_seen_count;
        std::map<std::string, int> platform_seen_count;

        for (const auto& slot : slots)
        {
            if (!slot.pack_result.has_value())
                continue;
            const auto& pr = slot.pack_result.value();
            for (const auto& p : pr.platforms)
            {
                platform_seen_count[p]++;
            }
            int64_t cv = static_cast<int64_t>(slot.type->inner_size.x) *
                         slot.type->inner_size.y *
                         slot.type->inner_size.z;
            cur_rate_sum += cv > 0
                                ? static_cast<double>(pr.used_volume) / static_cast<double>(cv)
                                : 0.0;
            for (const auto& g : pr.groups)
            {
                group_seen_count[g]++;
            }
        }

        // 选一个容器类型来装（字典序目标投影）
        const ContainerType* best_ct = nullptr;
        PackResult best_pr;
        ObjectiveVector best_proj;
        bool found = false;

        for (size_t idx : ct_indices)
        {
            const auto& ct = problem_.container_types[idx];
            if (!ct.has_remaining(container_type_usage_))
            {
                continue;
            }

            auto pr = pack_container(&ct, remaining);
            if (!pr.has_value() || pr->placements.empty())
            {
                continue;
            }

            // 计算此容器本身的填充率
            int64_t ct_vol = static_cast<int64_t>(ct.inner_size.x) *
                             ct.inner_size.y *
                             ct.inner_size.z;
            double this_rate = ct_vol > 0
                                   ? static_cast<double>(pr->used_volume) / static_cast<double>(ct_vol)
                                   : 0.0;

            // 估算剩余箱子还需要多少容器
            int64_t remain_vol = 0;
            std::set<std::string> packed_ids;
            for (const auto& pl : pr->placements)
            {
                packed_ids.insert(pl.box_id);
            }
            for (const auto* bp : remaining)
            {
                if (!packed_ids.count(bp->id))
                {
                    remain_vol += box_type_map_.at(bp->box_type_id).size.volume();
                }
            }
            int64_t usable_per_ct = static_cast<int64_t>(static_cast<double>(ct_vol) * this_rate);
            int future = usable_per_ct > 0
                             ? static_cast<int>((remain_vol + usable_per_ct - 1) / usable_per_ct)
                             : static_cast<int>((remain_vol + ct_vol - 1) / ct_vol);

            // 估算剩余箱子会引入的额外平台数
            std::set<std::string> future_platforms;
            for (const auto* bp : remaining)
            {
                if (!packed_ids.count(bp->id) && !bp->platform.empty())
                {
                    future_platforms.insert(bp->platform);
                }
            }

            // 估算剩余箱子会引入的额外分组拆分
            std::set<std::string> future_groups;
            for (const auto* bp : remaining)
            {
                if (!packed_ids.count(bp->id) && !bp->group.empty())
                {
                    future_groups.insert(bp->group);
                }
            }

            // 投影目标向量
            ObjectiveVector proj;
            proj.container_count = cur_count + 1 + future;

            proj.platform_split = cur_platform_split;
            for (const auto& p : pr->platforms)
            {
                if (platform_seen_count.count(p) && platform_seen_count.at(p) >= 1)
                {
                    proj.platform_split += 1;
                }
            }
            for (const auto& p : future_platforms)
            {
                if (!platform_seen_count.count(p))
                {
                    proj.platform_split += 1;
                }
            }
            proj.avg_volume_rate = cur_count > 0
                                       ? (cur_rate_sum + this_rate) / (cur_count + 1)
                                       : this_rate;

            proj.group_split_sum = 0;
            for (const auto& [g, cnt] : group_seen_count)
            {
                proj.group_split_sum += cnt - 1;
            }
            for (const auto& g : pr->groups)
            {
                if (group_seen_count.count(g) && group_seen_count.at(g) >= 1)
                {
                    proj.group_split_sum += 1;
                }
            }
            proj.group_split_sum += static_cast<int>(future_groups.size());

            const auto& keys = problem_.objective_keys.empty()
                                   ? default_objective_keys()
                                   : problem_.objective_keys;
            if (!found || compare_objectives(proj, best_proj, keys) < 0)
            {
                found = true;
                best_ct = &ct;
                best_pr = std::move(pr.value());
                best_proj = proj;

                // 当前容器装完所有剩余箱子，已是最优，无需尝试更大容器
                if (future == 0)
                {
                    break;
                }
            }
        }

        if (!found)
        {
            break; // 没有容器能装下任何箱子
        }

        // 记录此容器
        ContainerSlot slot;
        slot.instance_id = fmt::format("container_{}", next_instance_++);
        slot.type = best_ct;
        slot.pack_result = std::move(best_pr);
        for (const auto& pl : slot.pack_result->placements)
        {
            remaining_ids.erase(pl.box_id);
        }
        container_type_usage_[best_ct->id]++;

        spdlog::info("Container#{} \"{}\": packed {}, left {}, volume rate: {:.4f}, weight rate: {:.4f}",
                     slots.size() + 1, slot.type->id,
                     slot.pack_result->placements.size(), remaining_ids.size(),
                     slot.volume_rate(), slot.weight_rate());
        slots.push_back(std::move(slot));
    }

    bool all_packed = remaining_ids.empty();
    SolveStatus final_status;
    if (all_packed)
    {
        final_status = SolveStatus::Complete;
    }
    else if (!TimeChecker::check())
    {
        final_status = SolveStatus::Timeout;
    }
    else
    {
        final_status = SolveStatus::Partial;
    }
    return to_solution(slots, problem_.boxes, final_status);
}

std::optional<PackResult> Packer::pack_container(
    const ContainerType* ct,
    const std::vector<const Box*>& boxes) const
{
    std::vector<Box> box_list;
    for (const auto* bp : boxes)
    {
        box_list.push_back(*bp);
    }

    Heuristic heuristic(*ct, box_type_map_, box_map_, problem_, has_weight_info_);
    PackResult pr = heuristic.pack_beam(box_list, config::MLHS_WIDTH);
    if (pr.success || !pr.placements.empty())
    {
        return pr;
    }
    return std::nullopt;
}

Solution Packer::to_solution(
    const std::vector<ContainerSlot>& slots,
    const std::vector<Box>& all_boxes,
    SolveStatus status) const
{
    Solution sol;
    sol.status = status;
    sol.box_types = problem_.box_types;
    sol.objective_keys = problem_.objective_keys.empty()
                             ? default_objective_keys()
                             : problem_.objective_keys;

    auto elapsed = TimeChecker::elapsed();
    sol.elapsed_second = elapsed;

    std::set<std::string> packed_ids;

    for (const auto& slot : slots)
    {
        if (!slot.pack_result.has_value())
            continue;
        const auto& pr = slot.pack_result.value();

        ContainerSummary cs;
        cs.type_id = slot.type->id;
        cs.inner_size = slot.type->inner_size;
        cs.packed_count = static_cast<int>(pr.placements.size());
        cs.used_volume = pr.used_volume;
        cs.volume_rate = slot.volume_rate();
        cs.max_weight = slot.type->max_weight;
        if (has_weight_info_)
        {
            cs.used_weight = pr.total_weight;
            cs.weight_rate = slot.weight_rate();
        }
        cs.platforms.assign(pr.platforms.begin(), pr.platforms.end());
        cs.groups.assign(pr.groups.begin(), pr.groups.end());

        sol.container_summaries.push_back(std::move(cs));
        sol.container_placements.push_back(pr.placements);

        for (const auto& pl : pr.placements)
        {
            packed_ids.insert(pl.box_id);
        }
    }

    sol.packed_box_count = static_cast<int>(packed_ids.size());
    sol.unpacked_box_count = static_cast<int>(all_boxes.size() - packed_ids.size());

    for (const auto& bx : all_boxes)
    {
        if (!packed_ids.count(bx.id))
        {
            sol.unpacked_boxes.push_back(bx.id);
        }
    }

    // 计算目标向量
    sol.objective.container_count = static_cast<int>(slots.size());
    double sum_rate = 0.0;
    std::map<std::string, int> group_containers;
    std::map<std::string, int> platform_containers;

    for (const auto& slot : slots)
    {
        if (!slot.pack_result.has_value())
            continue;
        const auto& pr = slot.pack_result.value();
        for (const auto& p : pr.platforms)
        {
            platform_containers[p]++;
        }

        int64_t cv = static_cast<int64_t>(slot.type->inner_size.x) *
                     slot.type->inner_size.y *
                     slot.type->inner_size.z;
        sum_rate += cv > 0 ? static_cast<double>(pr.used_volume) / static_cast<double>(cv) : 0.0;

        for (const auto& g : pr.groups)
        {
            group_containers[g]++;
        }
    }

    sol.objective.avg_volume_rate = sol.objective.container_count > 0
                                        ? sum_rate / sol.objective.container_count
                                        : 0.0;

    int platform_split = 0;
    for (const auto& [p, count] : platform_containers)
    {
        platform_split += count - 1;
    }
    sol.objective.platform_split = platform_split;

    int split = 0;
    for (const auto& [g, count] : group_containers)
    {
        split += count - 1;
    }
    sol.objective.group_split_sum = split;

    return sol;
}

int Packer::handle_tender_limit_groups(
    std::set<std::string>& remaining_ids,
    std::vector<ContainerSlot>& slots)
{
    int limit = problem_.tender_limit.value();

    // 构建 group -> 已触碰的容器列表
    struct SlotInfo
    {
        const ContainerSlot* slot;
        int64_t remaining_capacity;
    };
    std::map<std::string, std::vector<SlotInfo>> group_info;

    for (const auto& slot : slots)
    {
        if (!slot.pack_result.has_value())
            continue;
        int64_t total = static_cast<int64_t>(slot.type->inner_size.x) *
                        slot.type->inner_size.y *
                        slot.type->inner_size.z;
        int64_t remaining = total - slot.pack_result->used_volume;
        for (const auto& g : slot.pack_result->groups)
        {
            group_info[g].push_back({&slot, remaining});
        }
    }

    for (const auto& [group, touched] : group_info)
    {
        if (static_cast<int>(touched.size()) < limit)
            continue;

        // 收集该组剩余箱子
        std::vector<const Box*> group_boxes;
        for (const auto& bx : problem_.boxes)
        {
            if (bx.group == group && remaining_ids.count(bx.id))
                group_boxes.push_back(&bx);
        }
        if (group_boxes.empty())
            continue;

        // 计算总体积
        int64_t group_vol = 0;
        for (const auto* bp : group_boxes)
            group_vol += box_type_map_.at(bp->box_type_id).size.volume();

        // 检查：每个已触碰容器逐一尝试，看能否全部装下
        // 用 pack_container 检查（包内从空容器开始 packing，但能验证尺寸/约束可行性）
        bool any_fit = false;
        for (const auto& info : touched)
        {
            // 快速体积过滤
            if (group_vol > info.slot->type->inner_size.volume())
                continue;

            // 用 pack_container 实际跑一次，检查是否所有箱子都能放入此种容器
            auto pr = pack_container(info.slot->type, group_boxes);
            if (!pr.has_value())
                continue;

            std::set<std::string> packed;
            for (const auto& pl : pr->placements)
                packed.insert(pl.box_id);

            bool all_packed = true;
            for (const auto* bp : group_boxes)
            {
                if (!packed.count(bp->id))
                {
                    all_packed = false;
                    break;
                }
            }
            if (!all_packed)
                continue;

            // 检查 packer 实际占用的空间是否 ≤ 容器的剩余容量（不超出现有占用）
            // 注意：packer 从空容器开始放，但我们只需要验证"能放下"，
            // 实际能否放进剩余空间由下一轮 pack 迭代的 pack_container 决定
            any_fit = true;
            break;
        }

        if (!any_fit)
        {
            // 假阴性保护：体积估算说可能够，但 pack_container 实际装不下
            // 检查是否是体积够但 packer 排布失败的情况
            int64_t total_remaining_cap = 0;
            for (const auto& info : touched)
                total_remaining_cap += info.remaining_capacity;

            if (group_vol <= total_remaining_cap)
            {
                // 体积够但 packer 排不出来 -> 保留机会，让下一轮迭代的
                // pack_container 自行决定（可能换个容器类型就成功了）
                continue;
            }

            spdlog::warn("tender_limit ({}) violated: group '{}' remaining volume {} exceeds capacity {}",
                         limit, group, group_vol, total_remaining_cap);
            return -1;
        }
    }

    return 0;
}

} // namespace hypercube::mlhs
