#include "order.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <set>
#include <unordered_set>

#include "../config.hpp"

namespace pack3d::rgs
{

namespace
{

std::set<int32_t> reachable_z_heights(const BoxType& bt) noexcept
{
    std::set<int32_t> zs;
    for (auto o : bt.allowed_orientations)
    {
        auto os = bt.size.orient(o);
        zs.insert(os.dz);
    }
    return zs;
}

// 箱型是否可被叠放（论文 §4.2 的可堆叠性 ϑ）：存在某朝向允许其上方放箱。
// max_stack 层号模型要求支撑箱 max_stack >= 2 才允许其上再放一层；max_load 须未设或 > 0。
bool type_stackable(const BoxType& bt) noexcept
{
    for (auto o : bt.allowed_orientations)
    {
        auto ms = bt.max_stack_for(o);
        if (ms.has_value() && ms.value() < 2)
        {
            continue;
        }
        auto ml = bt.max_load_for(o);
        if (ml.has_value() && ml.value() <= 0)
        {
            continue;
        }
        return true;
    }
    return false;
}

struct IdenticalGroup
{
    std::string box_type_id;
    std::set<int32_t> z_heights; // 可达高度集合（论文 §4.2.1：同一箱型可入多个组）
    std::vector<const Box*> boxes;
    int64_t total_volume = 0;
    int64_t max_volume = 0;
    int route_depth = -1;  // 组内箱子的最大 route 索引（无 route 平台为 -1）
    bool stackable = true; // 论文 ϑ：组内箱子是否可被叠放
};

struct SimilarGroup
{
    int32_t height = 0;     // 论文 Sh,ϑ 的 h：本组所有箱子均以高度 h 装载
    bool stackable = false; // 论文 ϑ：本组统一的可堆叠性（组键的一部分）
    std::vector<IdenticalGroup> identicals;
    int64_t total_volume = 0;
    int64_t max_volume = 0;
    int route_depth = -1; // 组内各 identical 组的最大 route 深度
};

int64_t box_volume(const BoxType& bt) noexcept
{
    return bt.size.volume();
}

} // anonymous namespace

std::vector<OrderEntry> build_ordered_list(
    const std::vector<Box>& remaining,
    const std::map<std::string, BoxType>& box_type_map,
    SortCriterion criterion,
    double rho,
    const std::optional<RouteOrder>& route) noexcept
{
    if (remaining.empty())
    {
        return {};
    }

    // ---- step 1: build identical groups ----
    std::map<std::string, IdenticalGroup> ig_map;

    for (const auto& bx : remaining)
    {
        auto bt_it = box_type_map.find(bx.box_type_id);
        if (bt_it == box_type_map.end())
        {
            continue;
        }
        const auto& bt = bt_it->second;

        auto& ig = ig_map[bx.box_type_id];
        if (ig.boxes.empty())
        {
            ig.box_type_id = bx.box_type_id;
            ig.z_heights = reachable_z_heights(bt);
            ig.max_volume = box_volume(bt);
            ig.stackable = type_stackable(bt);
        }
        ig.boxes.push_back(&bx);
        ig.total_volume += box_volume(bt);
    }

    // 计算各 identical 组的 route 深度（组内箱子的最大 route 索引）
    // route 存在时所有排序策略都按深度优先稳定排序，深处平台先占满 X 小侧
    if (route.has_value())
    {
        for (auto& [btid, ig] : ig_map)
        {
            ig.route_depth = -1;
            for (const auto* bx : ig.boxes)
            {
                if (bx->platform.empty())
                {
                    continue;
                }
                auto it = route->index_of.find(bx->platform);
                if (it != route->index_of.end())
                {
                    ig.route_depth = std::max(ig.route_depth, static_cast<int>(it->second));
                }
            }
        }
    }

    // ---- step 2: 按论文 §4.2.1 对每个 (可达高度 h, 可堆叠性 ϑ) 精确分组 ----
    // 每个 (h, ϑ) 一个 SimilarGroup（论文的 Sh,ϑ）；同一 IdenticalGroup 可达几个高度
    // 就入几个组（拷贝共享），配合 step 5 的朝向锁定实现层叠装载。可堆叠性入组键，
    // 使 Stackability* 排序的"可堆叠优先"在组内恒成立（组内 ϑ 一致）。
    std::map<std::pair<int32_t, bool>, SimilarGroup> sg_by_key;
    for (auto& [btid, ig] : ig_map)
    {
        for (int32_t h : ig.z_heights)
        {
            auto key = std::pair<int32_t, bool>{h, ig.stackable};
            auto it = sg_by_key.find(key);
            if (it == sg_by_key.end())
            {
                it = sg_by_key.emplace(key, SimilarGroup{}).first;
                it->second.height = h;
                it->second.stackable = ig.stackable;
            }
            SimilarGroup& sg = it->second;
            sg.identicals.push_back(ig);
            sg.total_volume += ig.total_volume;
            sg.max_volume = std::max(sg.max_volume, ig.max_volume);
            sg.route_depth = std::max(sg.route_depth, ig.route_depth);
        }
    }

    std::vector<SimilarGroup> sgs;
    sgs.reserve(sg_by_key.size());
    for (auto& [key, sg] : sg_by_key)
    {
        sgs.push_back(std::move(sg));
    }

    // ---- step 3: sort similar groups by criterion ----
    auto sg_cmp = [criterion](const SimilarGroup& a, const SimilarGroup& b) -> bool
    {
        switch (criterion)
        {
            case SortCriterion::StackabilityCumulatedVolume:
                if (a.stackable != b.stackable)
                {
                    return a.stackable; // 可堆叠优先
                }
                return a.total_volume > b.total_volume;

            case SortCriterion::StackabilityHighestVolume:
                if (a.stackable != b.stackable)
                {
                    return a.stackable; // 可堆叠优先
                }
                return a.max_volume > b.max_volume;

            case SortCriterion::CumulatedVolume:
                return a.total_volume > b.total_volume;

            case SortCriterion::HighestVolume:
                return a.max_volume > b.max_volume;

            case SortCriterion::Random:
            default:
                return false;
        }
    };

    static std::atomic<uint64_t> s_call_id{0};
    uint64_t call_id = s_call_id.fetch_add(1);
    // 使用 mt19937_64 而非 mt19937：64-bit 输出单次即可填充 double 的 53-bit 尾数，
    // uniform_real_distribution 映射更均匀；mt19937 需组合两个 32-bit 值，存在聚簇，
    // 经过 Shaw 的 pow(y, 1/rho) 非线性放大后会导致搜索轨迹系统性偏差（实测 _64 更优）。
    std::mt19937_64 rng(config::RANDOM_SEED + call_id);

    if (criterion == SortCriterion::Random)
    {
        std::shuffle(sgs.begin(), sgs.end(), rng);
    }
    else
    {
        std::sort(sgs.begin(), sgs.end(), sg_cmp);
    }

    // 路线纪律（仅确定性 pass，rho==0）：所有排序策略统一按 route 深度降序稳定排序
    // （深处平台先放，占满 X 小侧）。Shaw 迭代（rho>0）保持准则原排序 + 硬门校验，
    // 保留旧代码被验证的多样性（可合并布局、体积优先容器分配）。
    if (route.has_value() && rho == 0.0)
    {
        std::stable_sort(sgs.begin(), sgs.end(),
                         [](const SimilarGroup& a, const SimilarGroup& b)
                         { return a.route_depth > b.route_depth; });
    }

    // 组级 Shaw（论文 §4.2.2，仅 rho>0）：从有序列表逐位置抽取，第 j 个 =
    // ceil(y_j^(1/rho) * (n - j + 1))；rho 越大越随机，rho=0 接近原序。
    auto shaw = [&rng, rho](auto& work)
    {
        if (work.size() <= 1)
        {
            return work;
        }
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        size_t m = work.size();
        auto shuffled = work;
        shuffled.clear();
        shuffled.reserve(m);
        for (size_t j = 0; j < m && !work.empty(); ++j)
        {
            double y = dist(rng);
            size_t remaining_count = work.size();
            size_t pick = static_cast<size_t>(std::ceil(std::pow(y, 1.0 / rho) * remaining_count));
            if (pick == 0)
            {
                pick = 1;
            }
            if (pick > remaining_count)
            {
                pick = remaining_count;
            }
            shuffled.push_back(work[pick - 1]);
            work.erase(work.begin() + static_cast<ptrdiff_t>(pick - 1));
        }
        return shuffled;
    };

    // 相似组层 Shaw（仅 rho>0）：保持同组箱子相邻。Stackability* 准则下 sgs 已按
    // 可堆叠优先排序，对可堆叠/不可堆叠两段分别随机化，保持"可堆叠组严格优先于
    // 不可堆叠组"在随机化后仍然成立。
    if (rho > 0.0)
    {
        const bool stackability_split =
            (criterion == SortCriterion::StackabilityCumulatedVolume ||
             criterion == SortCriterion::StackabilityHighestVolume);
        if (stackability_split)
        {
            auto first_ns = std::find_if(sgs.begin(), sgs.end(),
                                         [](const SimilarGroup& g)
                                         { return !g.stackable; });
            std::vector<SimilarGroup> stackable_part(sgs.begin(), first_ns);
            std::vector<SimilarGroup> rest(first_ns, sgs.end());
            auto s1 = shaw(stackable_part);
            auto s2 = shaw(rest);
            s1.insert(s1.end(), s2.begin(), s2.end());
            sgs = std::move(s1);
        }
        else
        {
            sgs = shaw(sgs);
        }
    }

    // ---- step 4: within each similar group, sort identical groups ----
    bool use_total = (criterion == SortCriterion::CumulatedVolume ||
                      criterion == SortCriterion::StackabilityCumulatedVolume);
    for (auto& sg : sgs)
    {
        if (use_total)
        {
            std::sort(sg.identicals.begin(), sg.identicals.end(),
                      [](const IdenticalGroup& a, const IdenticalGroup& b)
                      { return a.total_volume > b.total_volume; });
        }
        else
        {
            std::sort(sg.identicals.begin(), sg.identicals.end(),
                      [](const IdenticalGroup& a, const IdenticalGroup& b)
                      { return a.max_volume > b.max_volume; });
        }

        // 组内同样按深度优先稳定排序（仅确定性 pass）
        if (route.has_value() && rho == 0.0)
        {
            std::stable_sort(sg.identicals.begin(), sg.identicals.end(),
                             [](const IdenticalGroup& a, const IdenticalGroup& b)
                             { return a.route_depth > b.route_depth; });
        }

        // 组内 identical 组层 Shaw（论文 §4.2.2，仅 rho>0）：组内 ϑ 一致，无需再分
        // 可堆叠子集；随机化后同组箱子仍相邻（层叠结构关键）。
        if (rho > 0.0)
        {
            sg.identicals = shaw(sg.identicals);
        }
    }

    // ---- step 5: flatten to OrderEntry list ----
    // 朝向锁定（论文 §4.2.3）：在高度 h 的组里只保留 dz == h 的朝向，层叠装载的
    // 关键机制。每个箱子最多出现 3 次（每个可达高度一次）；首次装入后其余出现由
    // insertion_heuristic 的 loaded_ids 跳过，装不下的则在后续高度重试。
    std::vector<OrderEntry> ordered;

    for (const auto& sg : sgs)
    {
        for (const auto& ig : sg.identicals)
        {
            auto bt_it = box_type_map.find(ig.box_type_id);
            if (bt_it == box_type_map.end())
            {
                continue;
            }
            const auto& bt = bt_it->second;

            std::vector<Orientation> locked;
            locked.reserve(bt.allowed_orientations.size());
            for (auto o : bt.allowed_orientations)
            {
                if (bt.size.orient(o).dz == sg.height)
                {
                    locked.push_back(o);
                }
            }
            if (locked.empty())
            {
                continue; // 防御：入组时已保证该高度可达
            }

            for (const auto* bx : ig.boxes)
            {
                ordered.push_back({bx->id, locked});
            }
        }
    }

    // ---- step 6: 随机化朝向（组级 Shaw 已在 step 3/4 完成，保持同组箱子相邻） ----
    if (rho > 0.0)
    {
        // shuffle orientations per entry（组内朝向顺序随机，不影响组间相邻性）
        for (auto& entry : ordered)
        {
            if (entry.orients.size() > 1)
            {
                std::shuffle(entry.orients.begin(), entry.orients.end(), rng);
            }
        }
    }

    return ordered;
}

} // namespace pack3d::rgs
