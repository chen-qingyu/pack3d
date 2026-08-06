#include "postprocess.hpp"

#include <algorithm>
#include <set>

#include <spdlog/spdlog.h>

#include "constraints.hpp"
#include "objectives.hpp"
#include "packer_base.hpp"
#include "tool.hpp"

namespace pack3d
{

namespace
{

// 剔除指定平台的箱子并重建容器的聚合字段（供捐献方容器使用）
ContainerLoad without_platform(const ContainerLoad& src,
                               const std::string& platform,
                               const std::map<std::string, Box>& box_map)
{
    ContainerLoad out;
    out.instance_id = src.instance_id;
    out.type_id = src.type_id;
    out.type = src.type;
    out.locked = src.locked;
    for (const auto& pl : src.placements)
    {
        if (pl.platform == platform)
        {
            continue;
        }
        out.placements.push_back(pl);
        out.used_volume += pl.osize.volume();
        if (!pl.platform.empty())
        {
            out.platforms.insert(pl.platform);
        }
        if (!pl.group.empty())
        {
            out.groups.insert(pl.group);
        }
        auto it = box_map.find(pl.box_id);
        if (it != box_map.end() && it->second.weight.has_value())
        {
            out.total_weight += it->second.weight.value();
        }
    }
    return out;
}

// 将分散在多个容器中的同平台箱子并入某个已有容器（绝不新增容器），
// 从而在不增加容器数量的前提下减少平台拆分数。
void reduce_platform_splits(std::vector<ContainerLoad>& all_loads,
                            ObjectiveVector& best_obj,
                            PackerBase& packer,
                            const std::map<std::string, BoxType>& box_type_map,
                            const std::map<std::string, Box>& box_map,
                            double support_rate,
                            bool has_max_stack,
                            bool has_max_load,
                            int tender_limit)
{
    std::map<std::string, std::set<size_t>> plat_containers;
    std::set<std::string> locked_platforms;
    for (size_t ci = 0; ci < all_loads.size(); ++ci)
    {
        const auto& cl = all_loads[ci];
        if (cl.locked)
        {
            locked_platforms.insert(cl.platforms.begin(), cl.platforms.end());
            continue;
        }
        for (const auto& p : cl.platforms)
        {
            plat_containers[p].insert(ci);
        }
    }

    // 待合并平台按"所在最靠尾容器"降序处理，让尾车（通常最空）优先吸收
    std::vector<std::string> cands;
    for (const auto& [p, cis] : plat_containers)
    {
        if (cis.size() <= 1 || locked_platforms.count(p))
        {
            continue;
        }
        cands.push_back(p);
    }
    std::sort(cands.begin(), cands.end(),
              [&](const std::string& a, const std::string& b)
              {
                  return *plat_containers[a].rbegin() > *plat_containers[b].rbegin();
              });

    bool improved = true;
    while (improved && TimeChecker::check())
    {
        improved = false;
        for (const auto& p : cands)
        {
            if (!TimeChecker::check())
            {
                return;
            }

            std::vector<size_t> cis;
            for (size_t ci = 0; ci < all_loads.size(); ++ci)
            {
                const auto& cl = all_loads[ci];
                if (!cl.locked && cl.platforms.count(p))
                {
                    cis.push_back(ci);
                }
            }
            if (cis.size() <= 1)
            {
                continue;
            }

            // 吸收方按剩余空间降序尝试（最空的通常是尾车，成功率最高）
            std::sort(cis.begin(), cis.end(),
                      [&](size_t a, size_t b)
                      {
                          int64_t fa = all_loads[a].total_volume() - all_loads[a].used_volume;
                          int64_t fb = all_loads[b].total_volume() - all_loads[b].used_volume;
                          return fa > fb;
                      });

            for (size_t target : cis)
            {
                std::vector<Box> donor_boxes;
                int64_t donor_vol = 0;
                for (size_t ci : cis)
                {
                    if (ci == target)
                    {
                        continue;
                    }
                    for (const auto& pl : all_loads[ci].placements)
                    {
                        if (pl.platform != p)
                        {
                            continue;
                        }
                        donor_vol += pl.osize.volume();
                        auto it = box_map.find(pl.box_id);
                        if (it != box_map.end())
                        {
                            donor_boxes.push_back(it->second);
                        }
                    }
                }
                const ContainerLoad& target_load = all_loads[target];
                int64_t free_vol = target_load.total_volume() - target_load.used_volume;
                if (donor_vol > free_vol)
                {
                    continue;
                }

                // 先试插入式：把捐献箱放入目标现有布局（保留目标布局，最省事）。
                // 失败则重排整个目标容器：目标自身箱子 + 捐献箱一起重新装载，
                // 碎片化布局也能重排容纳。目标必为非锁定容器（锁定容器已在 cis
                // 中排除），其内箱子全部可自由重排。
                // 合并期间捐献方容器仍带原 group，in-search tender 判定会过度收紧；
                // 故 trial 不启用 tender，改为对最终 candidate 整体复检
                ContainerLoad trial = packer.pack_single(donor_boxes, *target_load.type,
                                                         target_load.placements, TenderState{}, true);
                size_t expect = target_load.placements.size() + donor_boxes.size();
                if (trial.placements.size() != expect)
                {
                    std::vector<Box> rebox;
                    rebox.reserve(target_load.placements.size() + donor_boxes.size());
                    bool missing = false;
                    for (const auto& pl : target_load.placements)
                    {
                        auto it = box_map.find(pl.box_id);
                        if (it == box_map.end())
                        {
                            missing = true;
                            break;
                        }
                        rebox.push_back(it->second);
                    }
                    if (missing)
                    {
                        continue;
                    }
                    rebox.insert(rebox.end(), donor_boxes.begin(), donor_boxes.end());

                    trial = packer.pack_single(rebox, *target_load.type, {}, TenderState{}, true);
                    expect = rebox.size();
                    if (trial.placements.size() != expect)
                    {
                        continue;
                    }
                }

                std::vector<ContainerLoad> candidate;
                candidate.reserve(all_loads.size());
                std::string target_id = target_load.instance_id;
                for (size_t ci = 0; ci < all_loads.size(); ++ci)
                {
                    if (ci == target)
                    {
                        ContainerLoad merged = std::move(trial);
                        merged.instance_id = target_id;
                        candidate.push_back(std::move(merged));
                    }
                    else
                    {
                        ContainerLoad donor = without_platform(all_loads[ci], p, box_map);
                        if (!donor.placements.empty())
                        {
                            candidate.push_back(std::move(donor));
                        }
                    }
                }

                // tender 约束整体复检（合并后 group 分布可能改变连通结构）
                if (!check_all_tenders(candidate, tender_limit))
                {
                    continue;
                }

                // 移除底层箱子后，剩余箱子的支撑/堆码/承重可能被破坏，整体复检
                bool support_ok = true;
                for (auto& cl : candidate)
                {
                    for (const auto& pl : cl.placements)
                    {
                        if (!check_support(pl.position, pl.osize, cl, support_rate))
                        {
                            support_ok = false;
                            break;
                        }
                    }
                    if (!support_ok)
                    {
                        break;
                    }
                    if (has_max_stack || has_max_load)
                    {
                        std::vector<std::string> stack_errs;
                        recompute_stack_state(cl, box_type_map, &stack_errs);
                        if (!stack_errs.empty())
                        {
                            support_ok = false;
                            break;
                        }
                    }
                }
                if (!support_ok)
                {
                    continue;
                }

                auto cand_obj = compute_objective(candidate);
                if (compare_objectives(cand_obj, best_obj) < 0)
                {
                    all_loads = std::move(candidate);
                    best_obj = cand_obj;
                    spdlog::info("Consolidated platform {} into container {}", p, target_id);
                    improved = true;
                    break;
                }
            }
        }
    }
}

void repack_last_smaller(std::vector<ContainerLoad>& all_loads,
                         ObjectiveVector& best_obj,
                         PackerBase& packer,
                         const std::vector<ContainerType>& container_types,
                         const std::map<std::string, BoxType>& box_type_map,
                         const std::map<std::string, Box>& box_map,
                         int tender_limit)
{
    size_t i = all_loads.size() - 1;
    const auto& cl = all_loads[i];
    if (cl.placements.empty() || cl.locked)
    {
        return;
    }

    std::vector<Box> items;
    for (const auto& pl : cl.placements)
    {
        auto it = box_map.find(pl.box_id);
        if (it != box_map.end())
        {
            items.push_back(it->second);
        }
    }

    int64_t cur_vol = cl.type->inner_size.volume();

    std::vector<size_t> ct_indices;
    ct_indices.reserve(container_types.size());
    for (size_t ci = 0; ci < container_types.size(); ++ci)
    {
        ct_indices.push_back(ci);
    }
    std::sort(ct_indices.begin(), ct_indices.end(),
              [&](size_t a, size_t b)
              { return container_types[a].inner_size.volume() <
                       container_types[b].inner_size.volume(); });

    for (size_t ci : ct_indices)
    {
        const auto& ct = container_types[ci];
        if (ct.inner_size.volume() >= cur_vol)
        {
            continue;
        }

        bool all_fit = true;
        for (const auto& bx : items)
        {
            auto bt_it = box_type_map.find(bx.box_type_id);
            if (bt_it == box_type_map.end())
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
                    any_orient = true;
                    break;
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

        // 换小容器：新容器与旧容器同箱同 group，把旧容器视为"当前"排除，
        // 使 tender 判定镜像原解结构（原解已满足约束则必不误拒）
        TenderState tender = build_tender_state(all_loads, i, tender_limit);
        ContainerLoad new_load = packer.pack_single(items, ct, {}, tender, true);
        if (new_load.placements.size() != items.size())
        {
            continue;
        }

        std::vector<ContainerLoad> candidate;
        candidate.reserve(all_loads.size());
        for (size_t j = 0; j < all_loads.size(); ++j)
        {
            if (j == i)
            {
                candidate.push_back(std::move(new_load));
            }
            else
            {
                candidate.push_back(all_loads[j]);
            }
        }

        auto cand_obj = compute_objective(candidate);
        if (compare_objectives(cand_obj, best_obj) < 0)
        {
            auto old_type_id = cl.type->id;
            all_loads = std::move(candidate);
            best_obj = cand_obj;
            spdlog::info("Repacked container #{} from {} to {}",
                         i + 1, old_type_id, ct.id);
        }
        break;
    }
}

} // namespace

void postprocess(std::vector<ContainerLoad>& all_loads,
                 PackerBase& packer,
                 const std::vector<ContainerType>& container_types,
                 const std::map<std::string, BoxType>& box_type_map,
                 const std::map<std::string, Box>& box_map)
{
    if (all_loads.empty() || !TimeChecker::check())
    {
        return;
    }

    auto best_obj = compute_objective(all_loads);

    reduce_platform_splits(all_loads, best_obj, packer, box_type_map, box_map,
                           packer.support_rate(), packer.has_max_stack(),
                           packer.has_max_load(), packer.tender_limit());

    if (!TimeChecker::check() || all_loads.empty())
    {
        return;
    }

    repack_last_smaller(all_loads, best_obj, packer, container_types, box_type_map, box_map,
                        packer.tender_limit());
}

} // namespace pack3d
