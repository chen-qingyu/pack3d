#include "packer_base.hpp"

#include <spdlog/spdlog.h>

#include "io.hpp"
#include "objectives.hpp"
#include "postprocess.hpp"
#include "select_container.hpp"
#include "tool.hpp"

namespace pack3d
{

void prefill_load(ContainerLoad& load,
                  const std::vector<Placement>& existing,
                  const std::map<std::string, Box>& box_map)
{
    for (const auto& pl : existing)
    {
        load.placements.push_back(pl);
        load.used_volume += pl.osize.volume();
        if (!pl.platform.empty())
        {
            load.platforms.insert(pl.platform);
        }
        if (!pl.group.empty())
        {
            load.groups.insert(pl.group);
        }
        auto bx_it = box_map.find(pl.box_id);
        if (bx_it != box_map.end() && bx_it->second.weight.has_value())
        {
            load.total_weight += bx_it->second.weight.value();
        }
    }
}

Solution PackerBase::pack()
{
    TimeChecker::init(problem_.time_limit);

    std::vector<ContainerLoad> all_loads;
    std::vector<Box> remaining(problem_.boxes); // 未装箱集合，随装载推进原地剔除

    std::map<std::string, int> container_usage;
    int instance_counter = 0;

    // ---- 阶段 A: 预填充已有容器 ----
    // ct_map 只索引不拷贝，type 指针统一指向 problem_.container_types（生命周期覆盖整个求解过程）
    std::map<std::string, const ContainerType*> ct_map;
    for (const auto& ct : problem_.container_types)
    {
        ct_map[ct.id] = &ct;
    }
    std::map<std::string, BoxType> bt_map;
    for (const auto& bt : problem_.box_types)
    {
        bt_map[bt.id] = bt;
    }

    for (const auto& ec : problem_.existing_containers)
    {
        std::vector<std::string> build_errors;
        auto load = build_load_from_existing(ec, ct_map, bt_map, build_errors);
        if (build_errors.empty())
        {
            if (load.instance_id.empty())
            {
                load.instance_id = ec.type_id + "_" + std::to_string(instance_counter);
            }
            container_usage[ec.type_id]++;
            all_loads.push_back(std::move(load));
        }
    }
    instance_counter = static_cast<int>(all_loads.size());

    // ---- 阶段 B: 继续塞已有容器 ----
    for (size_t i = 0; i < all_loads.size(); ++i)
    {
        auto& cl = all_loads[i];
        if (remaining.empty() || !TimeChecker::check())
        {
            break;
        }

        auto ct_it = ct_map.find(cl.type_id);
        if (ct_it == ct_map.end())
        {
            continue;
        }

        // 将已有 placement 传给 pack_single 继续塞；当前容器不计入已提交 tender
        TenderState tender = build_tender_state(all_loads, i, problem_.tender_limit.value_or(0));
        ContainerLoad extra = pack_single(remaining, *ct_it->second, cl.placements, tender);
        if (extra.placements.size() <= cl.placements.size())
        {
            continue;
        }

        // 计算新增箱子 ID
        std::set<std::string> packed;
        {
            std::set<std::string> existing_ids;
            for (const auto& pl : cl.placements)
            {
                existing_ids.insert(pl.box_id);
            }
            for (const auto& pl : extra.placements)
            {
                if (!existing_ids.count(pl.box_id))
                {
                    packed.insert(pl.box_id);
                }
            }
        }

        if (packed.empty())
        {
            continue;
        }

        // extra 已含 existing + new 的累计值，直接覆盖
        cl.placements = std::move(extra.placements);
        cl.used_volume = extra.used_volume;
        cl.total_weight = extra.total_weight;
        cl.platforms = std::move(extra.platforms);
        cl.groups = std::move(extra.groups);
        std::erase_if(remaining, [&](const Box& b)
                      { return packed.count(b.id) != 0; });

        spdlog::info("Container \"{}\": added {}, now total {}, left {}",
                     cl.instance_id, packed.size(),
                     cl.placements.size(), remaining.size());
    }

    // ---- 阶段 C: 开新容器 ----
    while (!remaining.empty() && TimeChecker::check())
    {
        const ContainerType* ct = select_largest_fitting(
            problem_.container_types, container_usage, remaining, box_type_map_);
        if (!ct)
        {
            break;
        }

        // 新容器：全部已有容器为已提交 tender，自身不计入
        TenderState tender = build_tender_state(all_loads, all_loads.size(),
                                                problem_.tender_limit.value_or(0));
        ContainerLoad load = pack_single(remaining, *ct, {}, tender);
        load.instance_id = ct->id + "_" + std::to_string(instance_counter++);

        std::set<std::string> packed;
        for (const auto& pl : load.placements)
        {
            packed.insert(pl.box_id);
        }
        if (packed.empty())
        {
            // 剩余箱子被约束（如 tender）拒绝，任何容器都放不下 → 保持未装
            break;
        }
        std::erase_if(remaining, [&](const Box& b)
                      { return packed.count(b.id) != 0; });

        container_usage[ct->id]++;

        spdlog::info("Container#{} \"{}\": packed {}, left {}, volume rate: {:.4f}",
                     all_loads.size() + 1, ct->id, load.placements.size(),
                     remaining.size(), load.volume_rate());

        all_loads.push_back(std::move(load));
    }

    postprocess(all_loads, *this, problem_.container_types, box_type_map_, box_map_);

    return build_solution(all_loads, remaining);
}

Solution PackerBase::build_solution(
    const std::vector<ContainerLoad>& all_loads,
    const std::vector<Box>& remaining)
{
    Solution sol;
    sol.elapsed_second = TimeChecker::elapsed();

    int packed = 0;
    for (const auto& cl : all_loads)
    {
        packed += static_cast<int>(cl.placements.size());
    }
    sol.packed_box_count = packed;
    sol.unpacked_box_count = static_cast<int>(remaining.size());

    if (!TimeChecker::check())
    {
        sol.status = SolveStatus::Timeout;
    }
    else if (sol.unpacked_box_count == 0)
    {
        sol.status = SolveStatus::Complete;
    }
    else
    {
        sol.status = SolveStatus::Partial;
    }

    sol.objective = compute_objective(all_loads);
    sol.box_types = problem_.box_types;
    sol.unpacked_boxes.reserve(remaining.size());
    for (const auto& bx : remaining)
    {
        sol.unpacked_boxes.push_back(bx.id);
    }

    for (const auto& cl : all_loads)
    {
        ContainerSummary cs;
        cs.type_id = cl.type->id;
        cs.inner_size = cl.type->inner_size;
        cs.obstacles = cl.type->obstacles;
        cs.facets = cl.type->facets;
        cs.payload = cl.type->payload;
        cs.used_volume = cl.used_volume;
        cs.volume_rate = cl.volume_rate();
        cs.packed_count = static_cast<int>(cl.placements.size());
        if (has_weight_info_)
        {
            cs.used_weight = cl.total_weight;
            if (cl.type->payload.has_value())
            {
                cs.weight_rate = cl.total_weight / cl.type->payload.value();
            }
        }
        cs.platforms = std::vector<std::string>(cl.platforms.begin(), cl.platforms.end());
        cs.groups = std::vector<std::string>(cl.groups.begin(), cl.groups.end());
        sol.container_summaries.push_back(std::move(cs));
        sol.container_placements.push_back(cl.placements);
    }

    // 每个容器所属 tender 序号（按连通分量首次出现顺序，1-based；无 group 为 null）
    auto tenders = compute_container_tenders(all_loads);
    for (size_t i = 0; i < sol.container_summaries.size(); ++i)
    {
        sol.container_summaries[i].tender = tenders[i];
    }

    // 非 complete 时给出未装箱原因说明（violations）
    if (sol.status != SolveStatus::Complete && sol.unpacked_box_count > 0)
    {
        int tender_blocked = 0;
        std::set<std::string> blocked_groups;
        if (problem_.tender_limit.has_value())
        {
            const int limit = problem_.tender_limit.value();
            // 全部容器视为已提交：若某 group 连开新容器都会超限，则该箱必为 tender 所拒
            TenderState ts = build_tender_state(all_loads, all_loads.size(), limit);
            for (const auto& bx : remaining)
            {
                if (!bx.group.empty() && !check_tender_limit(ts, {}, bx.group))
                {
                    ++tender_blocked;
                    blocked_groups.insert(bx.group);
                }
            }
        }
        if (tender_blocked > 0)
        {
            std::string groups;
            for (const auto& g : blocked_groups)
            {
                if (!groups.empty())
                {
                    groups += ", ";
                }
                groups += g;
            }
            sol.violations.push_back(
                std::to_string(tender_blocked) + " box(es) not packed: exceeds tender_limit (max " +
                std::to_string(problem_.tender_limit.value()) + " containers per tender), groups: " +
                groups);
        }
        const int other = sol.unpacked_box_count - tender_blocked;
        if (other > 0)
        {
            sol.violations.push_back(
                std::to_string(other) + " box(es) not packed: " +
                (sol.status == SolveStatus::Timeout ? "time limit exceeded" : "no feasible placement"));
        }
    }

    return sol;
}

} // namespace pack3d
