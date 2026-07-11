#pragma once

#include <functional>
#include <set>
#include <vector>

#include <spdlog/spdlog.h>

#include "objectives.hpp"
#include "tool.hpp"
#include "types.hpp"

namespace pack3d
{

// PackFunc: 单容器填充回调，与 PackerBase::pack_single 同签名
template <typename PackFunc>
void postprocess(std::vector<ContainerLoad>& all_loads,
                 PackFunc&& pack_single,
                 const std::vector<ContainerType>& container_types,
                 const std::map<std::string, BoxType>& box_type_map,
                 const std::map<std::string, Box>& box_map)
{
    if (all_loads.empty())
    {
        return;
    }

    auto best_obj = compute_objective(all_loads);

    // Phase 1: repack_all_smaller — 每个容器尝试用更小容器重装
    if (TimeChecker::check())
    {
        for (size_t i = 0; i < all_loads.size() && TimeChecker::check(); ++i)
        {
            const auto& cl = all_loads[i];
            if (cl.placements.empty())
            {
                continue;
            }

            // 收集此容器的箱子
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

            // 按体积升序排列（优先尝试最小容器，等价于 downsize）
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

                // 维度快速检查
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

                // 重装
                ContainerLoad new_load = pack_single(items, ct);
                if (new_load.placements.size() != items.size())
                {
                    continue;
                }

                // 构建候选解
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
                    all_loads = std::move(candidate);
                    best_obj = cand_obj;
                    spdlog::debug("Repacked container {} from {} to {}",
                                  cl.instance_id, cl.type->id, ct.id);
                    break;
                }
            }
        }
    }

    // Phase 2: merge_platforms — 合并分散平台
    if (TimeChecker::check())
    {
        // 收集平台分布
        std::map<std::string, std::vector<size_t>> plat_containers;
        for (size_t ci = 0; ci < all_loads.size(); ++ci)
        {
            for (const auto& pl : all_loads[ci].placements)
            {
                if (!pl.platform.empty())
                {
                    plat_containers[pl.platform].push_back(ci);
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

            // 唯一化容器索引
            std::set<size_t> unique_cis(cis.begin(), cis.end());
            if (unique_cis.size() <= 1)
            {
                continue;
            }

            // 收集该平台所有箱子
            std::vector<Box> items;
            for (size_t ci : unique_cis)
            {
                for (const auto& pl : all_loads[ci].placements)
                {
                    if (pl.platform == plat)
                    {
                        auto it = box_map.find(pl.box_id);
                        if (it != box_map.end())
                        {
                            items.push_back(it->second);
                        }
                    }
                }
            }

            // 尝试装入一个容器
            for (const auto& ct : container_types)
            {
                ContainerLoad sload = pack_single(items, ct);
                if (sload.placements.size() != items.size())
                {
                    continue;
                }

                // 构建候选：移除原平台的箱子，加入合并容器
                std::vector<ContainerLoad> candidate;
                for (size_t ci = 0; ci < all_loads.size(); ++ci)
                {
                    ContainerLoad cl;
                    cl.instance_id = all_loads[ci].instance_id;
                    cl.type = all_loads[ci].type;
                    cl.type_id = all_loads[ci].type_id;
                    cl.total_weight = 0;
                    for (const auto& pl : all_loads[ci].placements)
                    {
                        if (pl.platform != plat)
                        {
                            cl.placements.push_back(pl);
                            cl.used_volume += pl.osize.volume();
                            if (!pl.platform.empty())
                            {
                                cl.platforms.insert(pl.platform);
                            }
                            if (!pl.group.empty())
                            {
                                cl.groups.insert(pl.group);
                            }
                            auto bx_it = box_map.find(pl.box_id);
                            if (bx_it != box_map.end() && bx_it->second.weight.has_value())
                            {
                                cl.total_weight += bx_it->second.weight.value();
                            }
                        }
                    }
                    if (!cl.placements.empty())
                    {
                        candidate.push_back(std::move(cl));
                    }
                }
                sload.instance_id = ct.id + "_merged_plat";
                candidate.push_back(std::move(sload));

                auto cand_obj = compute_objective(candidate);
                if (compare_objectives(cand_obj, best_obj) < 0)
                {
                    all_loads = std::move(candidate);
                    best_obj = cand_obj;
                    spdlog::debug("Merged platform {} into single container {}", plat, ct.id);
                }
                break;
            }
        }
    }
}

} // namespace pack3d
