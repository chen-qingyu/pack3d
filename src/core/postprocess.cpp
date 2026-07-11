#include "postprocess.hpp"

#include <algorithm>
#include <set>

#include <spdlog/spdlog.h>

#include "objectives.hpp"
#include "packer_base.hpp"
#include "tool.hpp"

namespace pack3d
{

void postprocess(std::vector<ContainerLoad>& all_loads,
                 PackerBase& packer,
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

                ContainerLoad new_load = packer.pack_single(items, ct);
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

            std::set<size_t> unique_cis(cis.begin(), cis.end());
            if (unique_cis.size() <= 1)
            {
                continue;
            }

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

            for (const auto& ct : container_types)
            {
                ContainerLoad sload = packer.pack_single(items, ct);
                if (sload.placements.size() != items.size())
                {
                    continue;
                }

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
