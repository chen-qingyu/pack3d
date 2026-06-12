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

    // 初始分配
    Assignment best = greedy_assign(problem_.boxes);

    // 局部搜索改进
    local_search(best, problem_.boxes);

    bool all_packed = true;
    size_t total_packed = 0;
    for (const auto& slot : best.slots)
    {
        if (slot.pack_result.has_value())
        {
            total_packed += slot.pack_result->placements.size();
        }
    }
    all_packed = (total_packed == problem_.boxes.size());

    return to_solution(best, problem_.boxes, all_packed,
                       all_packed ? reason::k_feasible : reason::k_no_solution);
}

GlobalScheduler::Assignment GlobalScheduler::greedy_assign(const std::vector<Box>& boxes)
{
    Assignment assign;

    // 按体积降序排列箱子
    std::vector<const Box*> sorted;
    for (const auto& bx : boxes)
    {
        sorted.push_back(&bx);
    }
    std::sort(sorted.begin(), sorted.end(),
              [&](const Box* a, const Box* b)
              {
                  auto& at = box_type_map_.at(a->box_type_id);
                  auto& bt = box_type_map_.at(b->box_type_id);
                  return at.size.volume() > bt.size.volume();
              });

    // 依次分配每个箱子
    for (const Box* box : sorted)
    {
        bool placed = false;

        // 尝试放入已有容器
        for (auto& slot : assign.slots)
        {
            // 模拟加入此箱子后重新打包
            std::vector<const Box*> trial_boxes;
            for (const auto& bid : slot.box_ids)
            {
                auto it = box_map_.find(bid);
                if (it != box_map_.end())
                {
                    trial_boxes.push_back(&it->second);
                }
            }
            trial_boxes.push_back(box);

            auto pr = pack_container(slot.type, trial_boxes);
            if (pr.has_value() && pr->success)
            {
                // 可行，更新分配
                slot.box_ids.push_back(box->id);
                slot.pack_result = std::move(pr);
                placed = true;
                break;
            }
        }

        if (!placed)
        {
            // 开新容器
            const ContainerType* best_ct = nullptr;
            std::optional<PackResult> best_pr;

            for (const auto& ct : problem_.container_types)
            {
                auto usage_it = container_type_usage_.find(ct.id);
                int used = (usage_it != container_type_usage_.end()) ? usage_it->second : 0;
                if (ct.quantity_limit.has_value() && used >= ct.quantity_limit.value())
                {
                    continue;
                }

                std::vector<const Box*> single_box = {box};
                auto pr = pack_container(&ct, single_box);
                if (pr.has_value() && pr->success)
                {
                    // 偏好体积更大的容器（有助于减少后续容器数）
                    if (!best_ct || ct.inner_size.volume() > best_ct->inner_size.volume())
                    {
                        best_ct = &ct;
                        best_pr = std::move(pr);
                    }
                }
            }

            if (best_ct)
            {
                ContainerSlot slot;
                slot.instance_id = fmt::format("container_{}", next_instance_++);
                slot.type = best_ct;
                slot.box_ids.push_back(box->id);
                slot.pack_result = std::move(best_pr);
                assign.slots.push_back(std::move(slot));
                container_type_usage_[best_ct->id]++;
            }
        }
    }

    // 补充：对未成功打包的容器重新尝试（首次分配时可能会有 packing 失败的 slot）
    repack_all(assign);
    assign.objective = compute_objective(assign);
    return assign;
}

void GlobalScheduler::local_search(Assignment& assign, const std::vector<Box>& all_boxes)
{
    std::map<std::string, const Box*> box_ptr_map;
    for (const auto& bx : all_boxes)
    {
        box_ptr_map[bx.id] = &bx;
    }

    bool improved = true;
    int iteration = 0;
    const int max_iterations = 100;

    while (improved && iteration < max_iterations && check_time())
    {
        improved = false;
        ++iteration;

        // 遍历所有容器对 (src, dst)，尝试将 src 中的一个箱子移到 dst
        for (size_t si = 0; si < assign.slots.size(); ++si)
        {
            for (size_t bi = 0; bi < assign.slots[si].box_ids.size(); ++bi)
            {
                if (!check_time())
                {
                    return;
                }

                const std::string& box_id = assign.slots[si].box_ids[bi];
                auto box_it = box_ptr_map.find(box_id);
                if (box_it == box_ptr_map.end())
                {
                    continue;
                }

                for (size_t di = 0; di < assign.slots.size(); ++di)
                {
                    if (si == di)
                    {
                        continue;
                    }

                    // 尝试 dst + box
                    std::vector<const Box*> dst_boxes;
                    for (const auto& bid : assign.slots[di].box_ids)
                    {
                        auto it = box_ptr_map.find(bid);
                        if (it != box_ptr_map.end())
                        {
                            dst_boxes.push_back(it->second);
                        }
                    }
                    dst_boxes.push_back(box_it->second);

                    auto dst_pr = pack_container(assign.slots[di].type, dst_boxes);
                    if (!dst_pr.has_value() || !dst_pr->success)
                    {
                        continue;
                    }

                    // 尝试 src - box
                    std::vector<const Box*> src_boxes;
                    for (const auto& bid : assign.slots[si].box_ids)
                    {
                        if (bid != box_id)
                        {
                            auto it = box_ptr_map.find(bid);
                            if (it != box_ptr_map.end())
                            {
                                src_boxes.push_back(it->second);
                            }
                        }
                    }

                    // 移动可行，提交（先收集新状态，再原子替换）
                    std::vector<ContainerSlot> new_slots;
                    for (size_t i = 0; i < assign.slots.size(); ++i)
                    {
                        if (i == si)
                        {
                            if (!src_boxes.empty())
                            {
                                auto src_pr = pack_container(assign.slots[i].type, src_boxes);
                                if (!src_pr.has_value() || !src_pr->success)
                                {
                                    continue; // 不应发生，跳过此候选
                                }
                                ContainerSlot new_slot = assign.slots[i];
                                new_slot.box_ids.clear();
                                for (const auto* bp : src_boxes)
                                {
                                    new_slot.box_ids.push_back(bp->id);
                                }
                                new_slot.pack_result = std::move(src_pr);
                                new_slots.push_back(std::move(new_slot));
                            }
                            // 否则 src 空了，不加入（被删除）
                        }
                        else if (i == di)
                        {
                            ContainerSlot new_slot = assign.slots[i];
                            new_slot.box_ids.push_back(box_id);
                            new_slot.pack_result = std::move(dst_pr);
                            new_slots.push_back(std::move(new_slot));
                        }
                        else
                        {
                            new_slots.push_back(assign.slots[i]);
                        }
                    }

                    // 验证新方案的目标是否不差于旧方案
                    Assignment new_assign;
                    new_assign.slots = std::move(new_slots);
                    new_assign.objective = compute_objective(new_assign);

                    if (!new_assign.is_better_than(assign))
                    {
                        continue; // 目标没改善，跳过
                    }

                    // 接受
                    assign = std::move(new_assign);
                    improved = true;
                    goto found_move;
                }
            }
        }
    found_move:;
    }

    if (iteration > 1)
    {
        spdlog::info("Local search: {} iterations, {} containers",
                     iteration, assign.slots.size());
    }
}

void GlobalScheduler::repack_all(Assignment& assign)
{
    for (auto& slot : assign.slots)
    {
        std::vector<const Box*> boxes;
        for (const auto& bid : slot.box_ids)
        {
            auto it = box_map_.find(bid);
            if (it != box_map_.end())
            {
                boxes.push_back(&it->second);
            }
        }
        slot.pack_result = pack_container(slot.type, boxes);
    }
}

std::optional<PackResult> GlobalScheduler::pack_container(
    const ContainerType* ct,
    const std::vector<const Box*>& boxes) const
{
    // 将 Box* 列表转为 Box 列表
    std::vector<Box> box_list;
    for (const auto* bp : boxes)
    {
        box_list.push_back(*bp);
    }

    ContainerPacker packer(*ct, box_type_map_, box_map_, problem_, has_weight_info_);
    PackResult pr = packer.pack(box_list);
    if (pr.success || !pr.placements.empty())
    {
        return pr;
    }
    return std::nullopt;
}

ObjectiveVector GlobalScheduler::compute_objective(const Assignment& assign) const noexcept
{
    ObjectiveVector ov;
    ov.container_count = static_cast<int>(assign.slots.size());

    double sum_rate = 0.0;
    std::map<std::string, int> group_containers;

    for (const auto& slot : assign.slots)
    {
        if (!slot.pack_result.has_value())
        {
            continue;
        }
        const auto& pr = slot.pack_result.value();
        ov.platform_count += static_cast<int>(pr.platforms.size());

        int64_t container_vol = static_cast<int64_t>(slot.type->inner_size.x) *
                                slot.type->inner_size.y *
                                slot.type->inner_size.z;
        double rate = container_vol > 0
                          ? static_cast<double>(pr.used_volume) / static_cast<double>(container_vol)
                          : 0.0;
        sum_rate += rate;

        for (const auto& g : pr.groups)
        {
            group_containers[g]++;
        }
    }

    ov.avg_volume_rate = ov.container_count > 0 ? sum_rate / ov.container_count : 0.0;

    int split = 0;
    for (const auto& [g, count] : group_containers)
    {
        split += count - 1;
    }
    ov.group_split_sum = split;

    return ov;
}

bool GlobalScheduler::Assignment::is_better_than(const Assignment& rhs) const noexcept
{
    return objective.is_better_than(rhs.objective);
}

Solution GlobalScheduler::to_solution(const Assignment& assign,
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
    sol.objective = assign.objective;

    auto elapsed = std::chrono::steady_clock::now() - start_time_;
    sol.elapsed_second = std::chrono::duration<double>(elapsed).count();

    // 统计打包/未打包的箱子
    std::set<std::string> packed_ids;
    for (const auto& slot : assign.slots)
    {
        if (!slot.pack_result.has_value())
        {
            continue;
        }
        const auto& pr = slot.pack_result.value();

        // ContainerSummary
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

    return sol;
}

bool GlobalScheduler::check_time() const
{
    auto elapsed = std::chrono::steady_clock::now() - start_time_;
    return std::chrono::duration<double>(elapsed).count() < problem_.time_limit_seconds;
}

} // namespace hypercube
