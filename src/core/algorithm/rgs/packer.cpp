#include "packer.hpp"

#include <algorithm>
#include <set>

#include <spdlog/spdlog.h>

#include "../../objectives.hpp"
#include "../../tool.hpp"
#include "../config.hpp"
#include "eval.hpp"
#include "insert.hpp"
#include "order.hpp"

namespace hypercube::rgs
{

Packer::Packer(
    const Problem& problem,
    const std::map<std::string, BoxType>& box_type_map,
    const std::map<std::string, ContainerType>& container_type_map,
    const std::map<std::string, Box>& box_map,
    bool has_weight_info)
    : problem_(problem)
    , box_type_map_(box_type_map)
    , container_type_map_(container_type_map)
    , box_map_(box_map)
    , has_weight_info_(has_weight_info)
{
}

const ContainerType* Packer::select_next_uld(
    const std::vector<Box>& remaining,
    const std::map<std::string, int>& container_usage) const noexcept
{
    if (remaining.empty())
    {
        return nullptr;
    }

    struct Candidate
    {
        const ContainerType* ct;
        int min_fit;
        int fit_count;
    };
    std::vector<Candidate> candidates;

    for (const auto& ct : problem_.container_types)
    {
        auto usage_it = container_usage.find(ct.id);
        int used = (usage_it != container_usage.end()) ? usage_it->second : 0;
        if (ct.quantity_limit.has_value() && used >= ct.quantity_limit.value())
        {
            continue;
        }

        int min_fit = INT32_MAX;
        int fit_count = 0;
        for (const auto& bx : remaining)
        {
            auto bt_it = box_type_map_.find(bx.box_type_id);
            if (bt_it == box_type_map_.end())
            {
                continue;
            }
            int u_i = count_fit_uld_types(bx, bt_it->second, problem_.container_types);
            bool fits = false;
            for (auto o : bt_it->second.allowed_orientations)
            {
                auto os = bt_it->second.size.orient(o);
                if (os.dx <= ct.inner_size.x && os.dy <= ct.inner_size.y && os.dz <= ct.inner_size.z)
                {
                    if (ct.max_weight.has_value() && bx.weight.has_value())
                    {
                        if (bx.weight.value() <= ct.max_weight.value() + 1e-9)
                        {
                            fits = true;
                            break;
                        }
                    }
                    else
                    {
                        fits = true;
                        break;
                    }
                }
            }
            if (fits)
            {
                min_fit = std::min(min_fit, u_i);
                ++fit_count;
            }
        }

        if (fit_count > 0)
        {
            candidates.push_back({&ct, min_fit, fit_count});
        }
    }

    if (candidates.empty())
    {
        return nullptr;
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b)
              {
                  if (a.min_fit != b.min_fit)
                  {
                      return a.min_fit < b.min_fit;
                  }
                  if (a.fit_count != b.fit_count)
                  {
                      return a.fit_count > b.fit_count;
                  }
                  return a.ct->inner_size.volume() > b.ct->inner_size.volume();
              });

    return candidates.front().ct;
}

ContainerLoad Packer::rgs_single_uld(
    const std::vector<Box>& items,
    const ContainerType& ctype,
    double penalty_denom) const noexcept
{
    static const SortCriterion criteria[] = {
        SortCriterion::StackabilityCumulatedVolume,
        SortCriterion::StackabilityHighestVolume,
        SortCriterion::CumulatedVolume,
        SortCriterion::HighestVolume,
        SortCriterion::Random,
    };
    constexpr int num_criteria = 5; // 排序策略数量，固定
    constexpr int min_per_crit = config::RGS_MIN_TOTAL / num_criteria;
    constexpr int max_per_crit = config::RGS_MAX_TOTAL / num_criteria;

    ContainerLoad best_load;
    double best_score = -1e9;

    // 第一轮：每个策略跑最低次数
    for (auto crit : criteria)
    {
        for (int iter = 0; iter < min_per_crit; ++iter)
        {
            if (!TimeChecker::check())
            {
                goto done;
            }

            double rho = (iter == 0) ? 0.0 : 0.5;
            ContainerLoad load;
            EpContext ctx;
            auto loaded_ids = insertion_heuristic(items, ctype, box_type_map_, crit, rho, problem_, load, ctx);

            std::set<std::string> loaded_set(loaded_ids.begin(), loaded_ids.end());
            std::vector<std::string> unloaded;
            for (const auto& bx : items)
            {
                if (!loaded_set.count(bx.id))
                {
                    unloaded.push_back(bx.id);
                }
            }

            double score = score_uld(load, unloaded, box_map_, box_type_map_,
                                     problem_.container_types, penalty_denom);
            if (score > best_score)
            {
                best_score = score;
                best_load = std::move(load);
            }
        }
    }

    // 第二轮：根据剩余时间跑更多迭代
    for (int iter = min_per_crit; iter < max_per_crit && TimeChecker::check(); ++iter)
    {
        for (auto crit : criteria)
        {
            if (!TimeChecker::check())
            {
                goto done;
            }

            double rho = 0.5;
            ContainerLoad load;
            EpContext ctx;
            auto loaded_ids = insertion_heuristic(items, ctype, box_type_map_, crit, rho, problem_, load, ctx);

            std::set<std::string> loaded_set(loaded_ids.begin(), loaded_ids.end());
            std::vector<std::string> unloaded;
            for (const auto& bx : items)
            {
                if (!loaded_set.count(bx.id))
                {
                    unloaded.push_back(bx.id);
                }
            }

            double score = score_uld(load, unloaded, box_map_, box_type_map_,
                                     problem_.container_types, penalty_denom);
            if (score > best_score)
            {
                best_score = score;
                best_load = std::move(load);
            }
        }
    }

done:
    if (best_load.type == nullptr)
    {
        best_load.type = &ctype; // 哨兵：无迭代完成时避免空指针
    }
    return best_load;
}

void Packer::rebuild_tracking(ContainerLoad& cl) noexcept
{
    cl.used_volume = 0;
    cl.total_weight = 0.0;
    cl.platforms.clear();
    cl.groups.clear();
    cl.platform_x_min.clear();
    cl.platform_x_max.clear();

    for (const auto& pl : cl.placements)
    {
        cl.used_volume += pl.osize.volume();
        if (!pl.platform.empty())
        {
            cl.platforms.insert(pl.platform);
            int32_t x2 = pl.position.x + pl.osize.dx;
            auto& xmin = cl.platform_x_min[pl.platform];
            auto& xmax = cl.platform_x_max[pl.platform];
            if (cl.platform_x_min.find(pl.platform) == cl.platform_x_min.end() || pl.position.x < xmin)
            {
                xmin = pl.position.x;
            }
            if (cl.platform_x_max.find(pl.platform) == cl.platform_x_max.end() || x2 > xmax)
            {
                xmax = x2;
            }
        }
        if (!pl.group.empty())
        {
            cl.groups.insert(pl.group);
        }
    }
}

Solution Packer::pack()
{
    TimeChecker::init(problem_.time_limit);

    double penalty_denom = compute_penalty_denom(problem_.boxes, box_type_map_, problem_.container_types);

    // ===== Alg7: 多 ULD 主循环 =====
    std::vector<Box> remaining = problem_.boxes;
    std::map<std::string, int> container_usage;
    std::vector<ContainerLoad> all_loads;
    int instance_counter = 0;

    while (!remaining.empty() && TimeChecker::check())
    {
        const ContainerType* ct = select_next_uld(remaining, container_usage);
        if (!ct)
        {
            spdlog::warn("No suitable container type for remaining {} boxes", remaining.size());
            break;
        }

        auto items_for_uld = remaining;
        std::sort(items_for_uld.begin(), items_for_uld.end(),
                  [](const Box& a, const Box& b)
                  {
                      if (a.group != b.group)
                      {
                          return a.group < b.group;
                      }
                      if (a.platform != b.platform)
                      {
                          return a.platform < b.platform;
                      }
                      return a.id < b.id;
                  });

        spdlog::debug("Selected ULD type: {}", ct->id);

        ContainerLoad load = rgs_single_uld(items_for_uld, *ct, penalty_denom);
        load.instance_id = ct->id + "_" + std::to_string(instance_counter++);

        std::set<std::string> loaded_set;
        for (const auto& pl : load.placements)
        {
            loaded_set.insert(pl.box_id);
        }
        std::vector<Box> new_remaining;
        for (const auto& bx : remaining)
        {
            if (!loaded_set.count(bx.id))
            {
                new_remaining.push_back(bx);
            }
        }
        int left = static_cast<int>(new_remaining.size());
        remaining = std::move(new_remaining);

        container_usage[ct->id]++;

        spdlog::info("Container#{} \"{}\": packed {}, left {}, volume rate: {:.4f}, weight rate: {:.4f}",
                     all_loads.size() + 1, ct->id, load.placements.size(), left,
                     load.volume_rate(),
                     ct->max_weight.has_value() && ct->max_weight.value() > 0
                         ? load.total_weight / ct->max_weight.value()
                         : 0.0);

        all_loads.push_back(std::move(load));
    }

    // ===== 后处理: 尝试将大容器物品重装到小容器 =====
    if (TimeChecker::check())
    {
        const auto& obj_keys = problem_.objective_keys.empty()
                                   ? default_objective_keys()
                                   : problem_.objective_keys;
        auto best_obj = compute_objective(all_loads);

        for (size_t i = 0; i < all_loads.size() && TimeChecker::check(); ++i)
        {
            const auto& cl = all_loads[i];
            if (cl.placements.empty())
            {
                continue;
            }

            std::vector<Box> items;
            for (const auto& pl : cl.placements)
            {
                auto it = box_map_.find(pl.box_id);
                if (it != box_map_.end())
                {
                    items.push_back(it->second);
                }
            }

            int64_t cur_vol = cl.type->inner_size.volume();
            for (const auto& ct : problem_.container_types)
            {
                if (ct.inner_size.volume() >= cur_vol)
                {
                    continue;
                }
                if (ct.id == cl.type->id)
                {
                    continue;
                }

                bool all_fit = true;
                for (const auto& bx : items)
                {
                    auto bt_it = box_type_map_.find(bx.box_type_id);
                    if (bt_it == box_type_map_.end())
                    {
                        all_fit = false;
                        break;
                    }
                    bool any_orient = false;
                    for (auto o : bt_it->second.allowed_orientations)
                    {
                        auto os = bt_it->second.size.orient(o);
                        if (os.dx <= ct.inner_size.x && os.dy <= ct.inner_size.y && os.dz <= ct.inner_size.z)
                        {
                            if (ct.max_weight.has_value() && bx.weight.has_value())
                            {
                                if (bx.weight.value() <= ct.max_weight.value() + 1e-9)
                                {
                                    any_orient = true;
                                    break;
                                }
                            }
                            else
                            {
                                any_orient = true;
                                break;
                            }
                        }
                    }
                    if (!any_orient)
                    {
                        all_fit = false;
                        break;
                    }
                }
                if (!all_fit)
                {
                    continue;
                }

                std::vector<ContainerLoad> split_loads;
                auto remain = items;
                bool fully_packed = true;
                while (!remain.empty() && TimeChecker::check())
                {
                    ContainerLoad sload;
                    EpContext sctx;
                    auto packed = insertion_heuristic(remain, ct, box_type_map_,
                                                      SortCriterion::StackabilityCumulatedVolume,
                                                      0.0, problem_, sload, sctx);
                    if (packed.empty())
                    {
                        fully_packed = false;
                        break;
                    }
                    sload.instance_id = ct.id + "_split_" + std::to_string(instance_counter++);
                    split_loads.push_back(std::move(sload));

                    std::set<std::string> pset(packed.begin(), packed.end());
                    std::vector<Box> new_remain;
                    for (const auto& bx : remain)
                    {
                        if (!pset.count(bx.id))
                        {
                            new_remain.push_back(bx);
                        }
                    }
                    remain = std::move(new_remain);
                }

                if (!fully_packed)
                {
                    continue;
                }

                std::vector<ContainerLoad> candidate;
                for (size_t j = 0; j < all_loads.size(); ++j)
                {
                    if (j != i)
                    {
                        candidate.push_back(all_loads[j]);
                    }
                }
                for (auto& sl : split_loads)
                {
                    candidate.push_back(std::move(sl));
                }

                auto cand_obj = compute_objective(candidate);
                if (compare_objectives(cand_obj, best_obj, obj_keys) < 0)
                {
                    all_loads = std::move(candidate);
                    best_obj = cand_obj;
                    spdlog::debug("Repacked ULD {} into {} x {} (obj improved)",
                                  cl.instance_id, split_loads.size(), ct.id);
                    goto try_repack_done;
                }
            }
        }
    try_repack_done:;
    }

    // ===== 后处理: 合并分散的同平台/同组物品 =====
    if (TimeChecker::check())
    {
        const auto& obj_keys = problem_.objective_keys.empty()
                                   ? default_objective_keys()
                                   : problem_.objective_keys;
        auto best_obj = compute_objective(all_loads);

        std::map<std::string, std::vector<size_t>> plat_containers;
        std::map<std::string, std::vector<size_t>> grp_containers;

        for (size_t ci = 0; ci < all_loads.size(); ++ci)
        {
            for (const auto& pl : all_loads[ci].placements)
            {
                if (!pl.platform.empty())
                {
                    plat_containers[pl.platform].push_back(ci);
                }
                if (!pl.group.empty())
                {
                    grp_containers[pl.group].push_back(ci);
                }
            }
        }

        for (const auto& [plat, cis] : plat_containers)
        {
            if (cis.size() <= 1)
            {
                continue;
            }
            if (!TimeChecker::check())
            {
                break;
            }

            std::vector<Box> items;
            for (size_t ci : cis)
            {
                for (const auto& pl : all_loads[ci].placements)
                {
                    if (pl.platform == plat)
                    {
                        auto it = box_map_.find(pl.box_id);
                        if (it != box_map_.end())
                        {
                            items.push_back(it->second);
                        }
                    }
                }
            }

            for (const auto& ct : problem_.container_types)
            {
                ContainerLoad sload;
                EpContext sctx;
                auto packed = insertion_heuristic(items, ct, box_type_map_,
                                                  SortCriterion::StackabilityCumulatedVolume,
                                                  0.0, problem_, sload, sctx);
                if (packed.size() != items.size())
                {
                    continue;
                }

                std::vector<ContainerLoad> candidate;
                for (size_t ci = 0; ci < all_loads.size(); ++ci)
                {
                    ContainerLoad cl;
                    cl.instance_id = all_loads[ci].instance_id;
                    cl.type = all_loads[ci].type;
                    for (const auto& pl : all_loads[ci].placements)
                    {
                        if (pl.platform != plat)
                        {
                            cl.placements.push_back(pl);
                        }
                    }
                    if (!cl.placements.empty())
                    {
                        rebuild_tracking(cl);
                        candidate.push_back(std::move(cl));
                    }
                }
                rebuild_tracking(sload);
                sload.instance_id = ct.id + "_merged_" + std::to_string(instance_counter++);
                candidate.push_back(std::move(sload));

                auto cand_obj = compute_objective(candidate);
                if (compare_objectives(cand_obj, best_obj, obj_keys) < 0)
                {
                    all_loads = std::move(candidate);
                    best_obj = cand_obj;
                    spdlog::debug("Merged platform {} into single container", plat);
                }
                break;
            }
        }

        for (const auto& [grp, cis] : grp_containers)
        {
            if (cis.size() <= 1)
            {
                continue;
            }
            if (!TimeChecker::check())
            {
                break;
            }

            std::vector<Box> items;
            for (size_t ci : cis)
            {
                for (const auto& pl : all_loads[ci].placements)
                {
                    if (pl.group == grp)
                    {
                        auto it = box_map_.find(pl.box_id);
                        if (it != box_map_.end())
                        {
                            items.push_back(it->second);
                        }
                    }
                }
            }

            for (const auto& ct : problem_.container_types)
            {
                ContainerLoad sload;
                EpContext sctx;
                auto packed = insertion_heuristic(items, ct, box_type_map_,
                                                  SortCriterion::StackabilityCumulatedVolume,
                                                  0.0, problem_, sload, sctx);
                if (packed.size() != items.size())
                {
                    continue;
                }

                std::vector<ContainerLoad> candidate;
                for (size_t ci = 0; ci < all_loads.size(); ++ci)
                {
                    ContainerLoad cl;
                    cl.instance_id = all_loads[ci].instance_id;
                    cl.type = all_loads[ci].type;
                    for (const auto& pl : all_loads[ci].placements)
                    {
                        if (pl.group != grp)
                        {
                            cl.placements.push_back(pl);
                        }
                    }
                    if (!cl.placements.empty())
                    {
                        rebuild_tracking(cl);
                        candidate.push_back(std::move(cl));
                    }
                }
                rebuild_tracking(sload);
                sload.instance_id = ct.id + "_merged_" + std::to_string(instance_counter++);
                candidate.push_back(std::move(sload));

                auto cand_obj = compute_objective(candidate);
                if (compare_objectives(cand_obj, best_obj, obj_keys) < 0)
                {
                    all_loads = std::move(candidate);
                    best_obj = cand_obj;
                    spdlog::debug("Merged group {} into single container", grp);
                }
                break;
            }
        }
    }

    // ===== Alg7 尾步: 降级最后一个 ULD =====
    if (!all_loads.empty() && TimeChecker::check())
    {
        const auto& last_load = all_loads.back();
        std::vector<Box> last_boxes;
        double last_total_weight = 0.0;
        for (const auto& pl : last_load.placements)
        {
            auto it = box_map_.find(pl.box_id);
            if (it != box_map_.end())
            {
                last_boxes.push_back(it->second);
                if (it->second.weight.has_value())
                {
                    last_total_weight += it->second.weight.value();
                }
            }
        }

        int64_t last_vol = last_load.type->inner_size.volume();
        const ContainerType* smaller = nullptr;
        for (const auto& ct : problem_.container_types)
        {
            auto usage_it = container_usage.find(ct.id);
            int used = (usage_it != container_usage.end()) ? usage_it->second : 0;
            if (ct.quantity_limit.has_value() && used >= ct.quantity_limit.value())
            {
                continue;
            }
            int64_t ct_vol = ct.inner_size.volume();
            if (ct_vol >= last_vol)
            {
                continue;
            }
            // 重量检查
            if (ct.max_weight.has_value() && last_total_weight > ct.max_weight.value() + 1e-9)
            {
                continue;
            }
            // 找体积最小的可用小容器
            if (smaller != nullptr && ct_vol >= smaller->inner_size.volume())
            {
                continue;
            }
            bool all_fit = true;
            for (const auto& bx : last_boxes)
            {
                auto bt_it = box_type_map_.find(bx.box_type_id);
                if (bt_it == box_type_map_.end())
                {
                    all_fit = false;
                    break;
                }
                bool box_fits = false;
                for (auto o : bt_it->second.allowed_orientations)
                {
                    auto os = bt_it->second.size.orient(o);
                    if (os.dx <= ct.inner_size.x && os.dy <= ct.inner_size.y && os.dz <= ct.inner_size.z)
                    {
                        box_fits = true;
                        break;
                    }
                }
                if (!box_fits)
                {
                    all_fit = false;
                    break;
                }
            }
            if (all_fit)
            {
                smaller = &ct;
            }
        }

        if (smaller != nullptr)
        {
            ContainerLoad repack = rgs_single_uld(last_boxes, *smaller, penalty_denom);
            std::set<std::string> repacked_set;
            for (const auto& pl : repack.placements)
            {
                repacked_set.insert(pl.box_id);
            }
            bool all_repacked = true;
            for (const auto& bx : last_boxes)
            {
                if (!repacked_set.count(bx.id))
                {
                    all_repacked = false;
                    break;
                }
            }
            if (all_repacked)
            {
                repack.instance_id = smaller->id + "_" + std::to_string(instance_counter++);
                container_usage[last_load.type->id]--;
                container_usage[smaller->id]++;
                all_loads.back() = std::move(repack);
                spdlog::debug("Downsized last ULD from {} to {}", last_load.type->id, smaller->id);
            }
        }
    }

    // ===== 构建 Solution =====
    Solution solution;
    solution.elapsed_second = TimeChecker::elapsed();

    int packed = 0;
    for (const auto& cl : all_loads)
    {
        packed += static_cast<int>(cl.placements.size());
    }
    solution.packed_box_count = packed;
    solution.unpacked_box_count = static_cast<int>(problem_.boxes.size()) - packed;
    if (!TimeChecker::check())
    {
        solution.status = SolveStatus::Timeout;
    }
    else if (solution.unpacked_box_count == 0)
    {
        solution.status = SolveStatus::Complete;
    }
    else
    {
        solution.status = SolveStatus::Partial;
    }
    solution.objective = compute_objective(all_loads);
    solution.objective_keys = problem_.objective_keys.empty()
                                  ? default_objective_keys()
                                  : problem_.objective_keys;
    solution.box_types = problem_.box_types;

    for (const auto& cl : all_loads)
    {
        ContainerSummary cs;
        cs.type_id = cl.type->id;
        cs.inner_size = cl.type->inner_size;
        cs.max_weight = cl.type->max_weight;
        cs.used_volume = cl.used_volume;
        cs.volume_rate = cl.volume_rate();
        cs.packed_count = static_cast<int>(cl.placements.size());
        if (has_weight_info_)
        {
            cs.used_weight = cl.total_weight;
            if (cl.type->max_weight.has_value())
            {
                cs.weight_rate = cl.total_weight / cl.type->max_weight.value();
            }
        }
        cs.platforms = std::vector<std::string>(cl.platforms.begin(), cl.platforms.end());
        cs.groups = std::vector<std::string>(cl.groups.begin(), cl.groups.end());
        solution.container_summaries.push_back(std::move(cs));
        solution.container_placements.push_back(cl.placements);
    }

    std::set<std::string> all_packed;
    for (const auto& cl : all_loads)
    {
        for (const auto& pl : cl.placements)
        {
            all_packed.insert(pl.box_id);
        }
    }
    for (const auto& bx : problem_.boxes)
    {
        if (!all_packed.count(bx.id))
        {
            solution.unpacked_boxes.push_back(bx.id);
        }
    }

    return solution;
}

} // namespace hypercube::rgs
