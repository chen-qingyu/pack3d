#include "palletizer.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "packer_base.hpp"
#include "pallet.hpp"
#include "tool.hpp"

namespace pack3d
{

namespace
{

bool is_palletize(const Box& bx, const std::map<std::string, BoxType>& bt_map)
{
    auto it = bt_map.find(bx.box_type_id);
    return it != bt_map.end() && it->second.loose;
}

// 托盘平台可用性：platforms 为空 = 全平台可用；非空仅命中平台可用（空串也视为普通平台）
bool pallet_available_for(const PalletType& pt, const std::string& platform) noexcept
{
    if (pt.platforms.empty())
    {
        return true;
    }
    return pt.platforms.count(platform) != 0;
}

// 候选按（装入体积, 装入箱数）字典序取优（天然倾向大托盘装满，托数最少）
struct Candidate
{
    ContainerLoad load;
    const PalletType* type = nullptr;

    bool better_than(const Candidate& o) const noexcept
    {
        if (load.used_volume != o.load.used_volume)
        {
            return load.used_volume > o.load.used_volume;
        }
        return load.placements.size() > o.load.placements.size();
    }
};

// 生成不与用户 id 冲突的 pallet_id：{pt.id}#{seq}（撞上则序号递增）
std::string next_pallet_id(const PalletType& pt, int& seq,
                           std::set<std::string>& used)
{
    while (true)
    {
        std::string id = pt.id + "#" + std::to_string(seq++);
        if (used.insert(id).second)
        {
            return id;
        }
    }
}

// 从候选 emit 一个 PalletLoad 并剔除已装箱子；返回 false 表示无可装
bool emit_pallet(std::vector<PalletLoad>& out,
                 std::set<std::string>& used_ids,
                 int& seq,
                 const Candidate& cand,
                 std::vector<Box>& remaining)
{
    if (cand.type == nullptr || cand.load.placements.empty())
    {
        return false;
    }
    PalletLoad p = make_pallet_load(cand.load, *cand.type,
                                    next_pallet_id(*cand.type, seq, used_ids));
    std::set<std::string> packed_ids;
    for (const auto& pl : p.placements)
    {
        packed_ids.insert(pl.box_id);
    }
    std::erase_if(remaining, [&](const Box& b)
                  { return packed_ids.count(b.id) != 0; });
    out.push_back(std::move(p));
    return true;
}

} // namespace

std::vector<PalletLoad> palletize(
    const Problem& problem,
    PackerBase& packer,
    std::vector<Box>& unpalletized)
{
    std::map<std::string, BoxType> bt_map;
    for (const auto& bt : problem.box_types)
    {
        bt_map[bt.id] = bt;
    }

    // 散件 = loose:true 箱型的箱子；已有容器中的箱子不参与装托
    std::vector<Box> remaining;
    for (const auto& bx : problem.boxes)
    {
        if (is_palletize(bx, bt_map))
        {
            remaining.push_back(bx);
        }
    }

    // 已占用 id（用户 box_types/boxes），pallet_id 生成须避开
    std::set<std::string> used_ids;
    for (const auto& bt : problem.box_types)
    {
        used_ids.insert(bt.id);
    }
    for (const auto& bx : problem.boxes)
    {
        used_ids.insert(bx.id);
    }
    int seq = 1;

    std::vector<PalletLoad> result;

    while (!remaining.empty() && TimeChecker::check())
    {
        // 分组（同组不拆托：先整组试装，装不下的组退回混合池）
        // 始终按 (platform, group) 分组：不同站点的货物不能混装一托
        std::map<std::string, std::vector<Box>> groups;
        for (const auto& bx : remaining)
        {
            std::string key = bx.platform + "\x1f" + encode_groups(bx.groups);
            groups[key].push_back(bx);
        }
        for (auto& [key, boxes] : groups)
        {
            std::sort(boxes.begin(), boxes.end(), [&](const Box& a, const Box& b)
                      { return bt_map.at(a.box_type_id).size.volume() >
                               bt_map.at(b.box_type_id).size.volume(); });
        }

        // 1) 整组试装：组内 >= 2 箱且整组装得下 → 该组独占一托
        Candidate best_group;
        for (const auto& pt : problem.pallet_types)
        {
            ContainerType ct = virtual_container(pt);
            for (const auto& [key, boxes] : groups)
            {
                if (boxes.size() < 2)
                {
                    continue;
                }
                if (!pallet_available_for(pt, boxes.front().platform))
                {
                    continue;
                }
                ContainerLoad load = packer.pack_single(boxes, ct, {}, TenderState{});
                load.type = nullptr; // ct 是栈上临时容器，避免悬挂指针
                if (load.placements.size() == boxes.size())
                {
                    Candidate cand{std::move(load), &pt};
                    if (best_group.type == nullptr || cand.better_than(best_group))
                    {
                        best_group = std::move(cand);
                    }
                }
            }
        }
        if (emit_pallet(result, used_ids, seq, best_group, remaining))
        {
            continue;
        }

        // 2) 混合兜底：托盘始终保持单一 platform；group 是否可混由配置决定
        Candidate best_mixed;
        std::map<std::string, std::vector<Box>> by_label;
        for (const auto& bx : remaining)
        {
            const std::string key = problem.pallet_mix_group.value()
                                        ? bx.platform
                                        : bx.platform + "\x1f" + encode_groups(bx.groups);
            by_label[key].push_back(bx);
        }
        for (const auto& pt : problem.pallet_types)
        {
            ContainerType ct = virtual_container(pt);
            for (const auto& entry : by_label)
            {
                const auto& boxes = entry.second;
                if (!pallet_available_for(pt, boxes.front().platform))
                {
                    continue;
                }
                ContainerLoad load = packer.pack_single(boxes, ct, {}, TenderState{});
                load.type = nullptr; // ct 是栈上临时容器，避免悬挂指针
                if (!load.placements.empty())
                {
                    Candidate cand{std::move(load), &pt};
                    if (best_mixed.type == nullptr || cand.better_than(best_mixed))
                    {
                        best_mixed = std::move(cand);
                    }
                }
            }
        }
        if (!emit_pallet(result, used_ids, seq, best_mixed, remaining))
        {
            break; // 剩余散件放不进任何托盘（处理见 pallet_fallback）
        }
    }

    unpalletized = std::move(remaining);
    return result;
}

Problem transform_pallet(
    const Problem& problem,
    const std::vector<PalletLoad>& pallet_loads,
    const std::vector<Box>& unpalletized,
    bool pallet_fallback)
{
    Problem np = problem;

    // 追加虚拟托盘箱型
    for (const auto& p : pallet_loads)
    {
        np.box_types.push_back(virtual_box_type(p));
    }

    // 重建 has_max_stack / has_max_load（虚拟托盘箱型自带 max_stack，须重新计算）
    bool has_ms = false;
    bool has_ml = false;
    for (const auto& bt : np.box_types)
    {
        for (const auto& v : bt.max_stack)
        {
            has_ms |= v.has_value();
        }
        for (const auto& v : bt.max_load)
        {
            has_ml |= v.has_value();
        }
    }
    np.has_max_stack = has_ms;
    np.has_max_load = has_ml;

    // boxes：虚拟托盘箱 + 普通箱子（原样保留）+（fallback=true 时）未装托散件
    std::map<std::string, bool> is_loose;
    for (const auto& bt : problem.box_types)
    {
        is_loose[bt.id] = bt.loose;
    }
    std::vector<Box> new_boxes;
    for (const auto& p : pallet_loads)
    {
        new_boxes.push_back(virtual_box(p));
    }
    for (const auto& bx : problem.boxes)
    {
        if (!is_loose[bx.box_type_id])
        {
            new_boxes.push_back(bx);
        }
    }
    if (pallet_fallback)
    {
        for (const auto& bx : unpalletized)
        {
            new_boxes.push_back(bx);
        }
    }
    np.boxes = std::move(new_boxes);
    return np;
}

void expand_pallet_solution(Solution& sol,
                            const std::vector<PalletLoad>& pallet_loads,
                            const std::vector<Box>& unpalletized,
                            bool pallet_fallback) noexcept
{
    sol.pallets = pallet_loads;
    sol.pallet_count = static_cast<int>(pallet_loads.size());

    // 每个托盘的内部箱数（容器内托盘单元按内部箱数计数，而非 1）
    std::map<std::string, int> inner_count;
    int palletized = 0;
    for (const auto& p : pallet_loads)
    {
        inner_count[p.pallet_id] = static_cast<int>(p.placements.size());
        palletized += static_cast<int>(p.placements.size());
    }
    sol.palletized_box_count = palletized;

    // 标记容器内的托盘单元（虚拟箱 box_id == pallet_id），供下游（web/draw）区分装托与散装
    for (auto& load : sol.container_placements)
    {
        for (auto& pl : load)
        {
            if (inner_count.count(pl.box_id) != 0)
            {
                pl.is_pallet = true;
            }
        }
    }

    // packed_box_count 折算原始散箱口径
    const int stage2_packed = sol.packed_box_count;
    for (size_t i = 0; i < sol.container_placements.size(); ++i)
    {
        int extra = 0;
        for (const auto& pl : sol.container_placements[i])
        {
            auto it = inner_count.find(pl.box_id);
            if (it != inner_count.end())
            {
                extra += it->second - 1;
            }
        }
        if (i < sol.container_summaries.size())
        {
            sol.container_summaries[i].packed_count += extra;
        }
    }
    sol.packed_box_count = stage2_packed - sol.pallet_count + palletized;
    sol.loose_box_count = stage2_packed - sol.pallet_count;

    // 未装托散件：fallback=false 时未进入装车阶段，补记未装箱（partial）
    if (!pallet_fallback && !unpalletized.empty())
    {
        for (const auto& bx : unpalletized)
        {
            sol.unpacked_boxes.push_back(bx.id);
        }
        sol.unpacked_box_count += static_cast<int>(unpalletized.size());
        if (sol.status == SolveStatus::Complete)
        {
            sol.status = SolveStatus::Partial;
        }
        for (const auto& b : unpalletized)
        {
            sol.violations.push_back("box " + b.id + " not palletized: cannot fit any pallet (set pallet_fallback=true to load loose)");
        }
    }
}

} // namespace pack3d
