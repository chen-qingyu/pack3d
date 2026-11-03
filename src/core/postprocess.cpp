#include "postprocess.hpp"

#include <algorithm>
#include <set>
#include <tuple>

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
        for (const auto& group : pl.groups)
        {
            out.groups.insert(group);
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

                // 危险品分柜硬校验：合并不得破坏"仅最后一车混装"的不变量
                if (!check_danger_segregation(candidate))
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
        if (it == box_map.end())
        {
            return; // 缺失箱子的放置无法安全重排，放弃该候选
        }
        items.push_back(it->second);
    }

    int64_t cur_vol = cl.type->inner_size.volume();

    // 换柜型前的两道下界剪枝，都取乐观值，只会漏判可行、不会误杀可行：
    // 1) 体积下界：全部箱子体积和超过候选柜容积，任何摆法都装不下；
    // 2) 底面积下界：所需底面积 ≥ 各箱最小底面积之和 ÷ 可堆层数（层数按候选柜
    //    高度与各箱 max_stack 取小）。带形状信息，比体积下界紧得多，多层场景也适用。
    // 没有它们，每个更小的柜型都要完整重跑一次求解，大实例上会吃光时间预算。
    int64_t total_box_volume = 0;
    int64_t total_min_area = 0;
    int32_t min_box_dz = 0; // 0 = 尚未确定
    int32_t stack_cap = 0;  // 0 = 不限层数
    bool shape_bounds_ok = true;

    for (const auto& bx : items)
    {
        auto bt_it = box_type_map.find(bx.box_type_id);
        if (bt_it == box_type_map.end())
        {
            shape_bounds_ok = false;
            break;
        }
        const auto& bt = bt_it->second;
        int64_t best_area = -1;
        int32_t best_dz = 0;
        int32_t box_cap = 0; // 该箱全部朝向下最严格的 max_stack，0 = 不限
        for (auto o : bt.allowed_orientations)
        {
            const auto os = bt.size.orient(o);
            const int64_t area = static_cast<int64_t>(os.dx) * os.dy;
            if (best_area < 0 || area < best_area)
            {
                best_area = area;
            }
            if (best_dz == 0 || os.dz < best_dz)
            {
                best_dz = os.dz;
            }
            const auto ms = bt.max_stack_for(o);
            if (ms.has_value() && (box_cap == 0 || ms.value() < box_cap))
            {
                box_cap = ms.value();
            }
        }
        if (best_area <= 0 || best_dz <= 0)
        {
            shape_bounds_ok = false;
            break;
        }
        total_min_area += best_area;
        total_box_volume += bt.size.volume();
        if (min_box_dz == 0 || best_dz < min_box_dz)
        {
            min_box_dz = best_dz;
        }
        if (box_cap > 0 && (stack_cap == 0 || box_cap < stack_cap))
        {
            stack_cap = box_cap;
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
        // 每个候选柜型都要完整重跑一次单容器求解，大实例上单次就可能是数秒；
        // 逐个试完会吃光时间预算并把结果打成 timeout，超时即停，保留当前解。
        if (!TimeChecker::check())
        {
            break;
        }

        const auto& ct = container_types[ci];
        if (ct.inner_size.volume() >= cur_vol)
        {
            continue;
        }
        // 体积下界：箱子总体积就超过候选柜容积
        if (ct.inner_size.volume() < total_box_volume)
        {
            continue;
        }
        // 底面积下界：各箱最小底面积之和 ÷ 按柜高与 max_stack 算出的可堆层数
        if (shape_bounds_ok && min_box_dz > 0)
        {
            int64_t layers = ct.inner_size.z / min_box_dz;
            if (stack_cap > 0 && layers > stack_cap)
            {
                layers = stack_cap;
            }
            if (layers < 1)
            {
                layers = 1;
            }
            const int64_t required_area = (total_min_area + layers - 1) / layers;
            if (static_cast<int64_t>(ct.inner_size.x) * ct.inner_size.y < required_area)
            {
                continue;
            }
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

        if (!check_danger_segregation(candidate))
        {
            continue;
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

// 同型连续柱按重量稳定排序（更重在下）：只重排"同箱型、同朝向、同一底面(x,y)、连续堆叠"的
// 规则柱；同型同朝向 → 各层 footprint 相同，仅改 z 顺序，几何与容积不变，让输出更合理。非约束。
void sort_stack_weights(std::vector<ContainerLoad>& all_loads)
{
    for (auto& cl : all_loads)
    {
        if (cl.placements.size() < 2)
        {
            continue;
        }
        // 分组键：(box_type_id, osize, x, y) —— 同型、同朝向、同一底面的规则柱
        std::map<std::tuple<std::string, int32_t, int32_t, int32_t, int32_t, int32_t>,
                 std::vector<size_t>>
            groups;
        for (size_t i = 0; i < cl.placements.size(); ++i)
        {
            const auto& pl = cl.placements[i];
            groups[{pl.box_type_id, pl.osize.dx, pl.osize.dy, pl.osize.dz,
                    pl.position.x, pl.position.y}]
                .push_back(i);
        }
        for (auto& entry : groups)
        {
            auto& idxs = entry.second;
            if (idxs.size() < 2)
            {
                continue;
            }
            std::sort(idxs.begin(), idxs.end(), [&](size_t a, size_t b)
                      { return cl.placements[a].position.z < cl.placements[b].position.z; });
            const auto dz = cl.placements[idxs[0]].osize.dz;
            bool contiguous = true;
            for (size_t k = 1; k < idxs.size(); ++k)
            {
                if (cl.placements[idxs[k]].position.z - cl.placements[idxs[k - 1]].position.z != dz)
                {
                    contiguous = false;
                    break;
                }
            }
            if (!contiguous)
            {
                continue;
            }
            bool ordered = true;
            for (size_t k = 1; k < idxs.size(); ++k)
            {
                if (cl.placements[idxs[k]].weight.value_or(0.0) >
                    cl.placements[idxs[k - 1]].weight.value_or(0.0) + 1e-9)
                {
                    ordered = false;
                    break;
                }
            }
            if (ordered)
            {
                continue;
            }
            std::vector<size_t> sorted = idxs;
            std::stable_sort(sorted.begin(), sorted.end(), [&](size_t a, size_t b)
                             { return cl.placements[a].weight.value_or(0.0) >
                                      cl.placements[b].weight.value_or(0.0); });
            const auto base_z = cl.placements[idxs[0]].position.z;
            for (size_t k = 0; k < sorted.size(); ++k)
            {
                cl.placements[sorted[k]].position.z =
                    base_z + static_cast<int32_t>(k) * dz;
            }
        }
    }
}

} // namespace

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

    if (TimeChecker::check())
    {
        reduce_platform_splits(all_loads, best_obj, packer, box_type_map, box_map,
                               packer.support_rate(), packer.has_max_stack(),
                               packer.has_max_load(), packer.tender_limit());
    }

    if (TimeChecker::check())
    {
        repack_last_smaller(all_loads, best_obj, packer, container_types,
                            box_type_map, box_map, packer.tender_limit());
    }

    if (TimeChecker::check())
    {
        sort_stack_weights(all_loads);
    }
}

} // namespace pack3d
