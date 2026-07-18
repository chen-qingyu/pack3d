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

void merge_platforms(std::vector<ContainerLoad>& all_loads,
                     ObjectiveVector& best_obj,
                     PackerBase& packer,
                     const std::vector<ContainerType>& container_types,
                     const std::map<std::string, BoxType>& box_type_map,
                     const std::map<std::string, Box>& box_map,
                     double support_rate)
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
        int64_t items_volume = 0;
        for (size_t ci : unique_cis)
        {
            for (const auto& pl : all_loads[ci].placements)
            {
                if (pl.platform == plat)
                {
                    items_volume += pl.osize.volume();
                    auto it = box_map.find(pl.box_id);
                    if (it != box_map.end())
                    {
                        items.push_back(it->second);
                    }
                }
            }
        }

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
            if (items_volume > ct.inner_size.volume())
            {
                continue;
            }
            ContainerLoad sload = packer.pack_single(items, ct, true);
            if (sload.placements.size() != items.size())
            {
                continue;
            }

            std::vector<ContainerLoad> candidate;
            for (const auto& src : all_loads)
            {
                ContainerLoad cl;
                cl.instance_id = src.instance_id;
                cl.type = src.type;
                cl.type_id = src.type_id;
                cl.total_weight = 0;
                for (const auto& pl : src.placements)
                {
                    if (pl.platform == plat)
                    {
                        continue;
                    }
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
                    auto it = box_map.find(pl.box_id);
                    if (it != box_map.end() && it->second.weight.has_value())
                    {
                        cl.total_weight += it->second.weight.value();
                    }
                }
                if (!cl.placements.empty())
                {
                    candidate.push_back(std::move(cl));
                }
            }
            sload.instance_id = ct.id + "_merged_plat";
            candidate.push_back(std::move(sload));

            // 验证合并后剩余箱子支撑是否仍然满足约束
            bool support_ok = true;
            for (const auto& cl : candidate)
            {
                for (const auto& pl : cl.placements)
                {
                    if (!check_support(pl.position, pl.osize, cl, box_type_map, support_rate))
                    {
                        support_ok = false;
                        break;
                    }
                }
                if (!support_ok)
                {
                    break;
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
                spdlog::info("Merged platform {} into single container {}", plat, ct.id);
            }
            break;
        }
    }
}

void repack_last_smaller(std::vector<ContainerLoad>& all_loads,
                         ObjectiveVector& best_obj,
                         PackerBase& packer,
                         const std::vector<ContainerType>& container_types,
                         const std::map<std::string, BoxType>& box_type_map,
                         const std::map<std::string, Box>& box_map)
{
    size_t i = all_loads.size() - 1;
    const auto& cl = all_loads[i];
    if (cl.placements.empty())
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

        ContainerLoad new_load = packer.pack_single(items, ct, true);
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

    merge_platforms(all_loads, best_obj, packer, container_types, box_type_map, box_map,
                    packer.support_rate());

    if (!TimeChecker::check() || all_loads.empty())
    {
        return;
    }

    repack_last_smaller(all_loads, best_obj, packer, container_types, box_type_map, box_map);
}

} // namespace pack3d
