#include "scheduler.hpp"

#include <algorithm>
#include <cassert>
#include <set>

#include <spdlog/spdlog.h>

#include "block.hpp"
#include "objectives.hpp"

namespace hypercube
{

GlobalScheduler::GlobalScheduler(
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

Solution GlobalScheduler::schedule()
{
    start_time_ = std::chrono::steady_clock::now();
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

    while (!remaining_ids.empty() && check_time())
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

        // 选一个容器类型来装
        const ContainerType* best_ct = nullptr;
        PackResult best_pr;
        size_t best_count = 0;

        for (const auto& ct : problem_.container_types)
        {
            auto it = container_type_usage_.find(ct.id);
            int used = (it != container_type_usage_.end()) ? it->second : 0;
            if (ct.quantity_limit.has_value() && used >= ct.quantity_limit.value())
            {
                continue;
            }

            auto pr = pack_container(&ct, remaining);
            if (pr.has_value() && pr->placements.size() > best_count)
            {
                best_count = pr->placements.size();
                best_ct = &ct;
                best_pr = std::move(pr.value());
            }
        }

        if (best_ct == nullptr || best_count == 0)
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
        slots.push_back(std::move(slot));
        container_type_usage_[best_ct->id]++;
    }

    // 日志输出
    int idx = 1;
    int packed_sofar = 0;
    int total_boxes = static_cast<int>(problem_.boxes.size());
    for (const auto& slot : slots)
    {
        if (!slot.pack_result.has_value())
            continue;
        const auto& pr = slot.pack_result.value();
        int packed = static_cast<int>(pr.placements.size());
        packed_sofar += packed;
        int left = total_boxes - packed_sofar;
        int64_t cv = static_cast<int64_t>(slot.type->inner_size.x) *
                     slot.type->inner_size.y *
                     slot.type->inner_size.z;
        double vr = cv > 0 ? static_cast<double>(pr.used_volume) / static_cast<double>(cv) * 100.0 : 0.0;
        double wr = has_weight_info_ && slot.type->max_weight.has_value() && slot.type->max_weight.value() > 0
                        ? pr.total_weight / slot.type->max_weight.value() * 100.0
                        : 0.0;
        spdlog::info("Container#{} \"{}\": packed {}, left {}, volume rate: {:.2f}%, weight rate: {:.2f}%",
                     idx, slot.type->id, packed, left, vr, wr);
        ++idx;
    }

    bool all_packed = remaining_ids.empty();
    return to_solution(slots, problem_.boxes, all_packed,
                       all_packed ? reason::k_feasible : reason::k_no_solution);
}

std::optional<PackResult> GlobalScheduler::pack_container(
    const ContainerType* ct,
    const std::vector<const Box*>& boxes) const
{
    std::vector<Box> box_list;
    for (const auto* bp : boxes)
    {
        box_list.push_back(*bp);
    }

    ContainerPacker packer(*ct, box_type_map_, box_map_, problem_, has_weight_info_);
    PackResult pr;
    if (problem_.algorithm.width > 1)
    {
        pr = packer.pack_beam(box_list, problem_.algorithm.width);
    }
    else
    {
        pr = packer.pack(box_list);
    }
    if (pr.success || !pr.placements.empty())
    {
        return pr;
    }
    return std::nullopt;
}

Solution GlobalScheduler::to_solution(
    const std::vector<ContainerSlot>& slots,
    const std::vector<Box>& all_boxes,
    bool success,
    const std::string& reason) const
{
    Solution sol;
    sol.success = success;
    sol.reason = reason;
    sol.box_types = problem_.box_types;
    sol.objective_keys = problem_.objective_keys.empty()
                             ? default_objective_keys()
                             : problem_.objective_keys;

    auto elapsed = std::chrono::steady_clock::now() - start_time_;
    sol.elapsed_second = std::chrono::duration<double>(elapsed).count();

    std::set<std::string> packed_ids;

    for (const auto& slot : slots)
    {
        if (!slot.pack_result.has_value())
            continue;
        const auto& pr = slot.pack_result.value();

        ContainerSummary cs;
        cs.id = slot.instance_id;
        cs.type_id = slot.type->id;
        cs.inner_size = slot.type->inner_size;
        cs.used_volume = pr.used_volume;
        cs.volume_rate = slot.type->inner_size.volume() > 0
                             ? static_cast<double>(pr.used_volume) / static_cast<double>(slot.type->inner_size.volume())
                             : 0.0;
        cs.max_weight = slot.type->max_weight;
        if (has_weight_info_)
        {
            cs.used_weight = pr.total_weight;
            if (slot.type->max_weight.has_value() && slot.type->max_weight.value() > 0)
            {
                cs.weight_rate = pr.total_weight / slot.type->max_weight.value();
            }
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
    sol.objective.platform_count = 0;
    double sum_rate = 0.0;
    std::map<std::string, int> group_containers;

    for (const auto& slot : slots)
    {
        if (!slot.pack_result.has_value())
            continue;
        const auto& pr = slot.pack_result.value();
        sol.objective.platform_count += static_cast<int>(pr.platforms.size());

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

    int split = 0;
    for (const auto& [g, count] : group_containers)
    {
        split += count - 1;
    }
    sol.objective.group_split_sum = split;

    return sol;
}

bool GlobalScheduler::check_time() const
{
    auto elapsed = std::chrono::steady_clock::now() - start_time_;
    return std::chrono::duration<double>(elapsed).count() < problem_.time_limit;
}

} // namespace hypercube
