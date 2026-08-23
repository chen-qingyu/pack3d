#include "packer.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <vector>

#include <spdlog/spdlog.h>

#include "../../tool.hpp"
#include "../config.hpp"
#include "grid.hpp"
#include "insert.hpp"
#include "order.hpp"

namespace pack3d
{

namespace
{

// 用已有放置预填充 EP 上下文（load 聚合字段由共享的 prefill_load 完成）
void prefill_ep(rgs::EpContext& ctx, const std::vector<Placement>& existing)
{
    for (const auto& pl : existing)
    {
        ctx.extreme_points.insert({pl.position.x + pl.osize.dx, pl.position.y, pl.position.z});
        ctx.extreme_points.insert({pl.position.x, pl.position.y + pl.osize.dy, pl.position.z});
        ctx.extreme_points.insert({pl.position.x, pl.position.y, pl.position.z + pl.osize.dz});
    }
}

// 剩余箱子的平台数：未放入当前容器的箱子中，去重后的非空平台数。
// 对齐字典序目标 min_platform_split（同 BSG 的 remaining_platform_count）：
// 体积并列时选剩余平台更少的装箱，倾向把平台聚拢，降低后处理救不回的拆分概率。
size_t count_remaining_platforms(const std::vector<Box>& items,
                                 const ContainerLoad& load) noexcept
{
    std::set<std::string> placed;
    for (const auto& pl : load.placements)
    {
        placed.insert(pl.box_id);
    }
    std::set<std::string> platforms;
    for (const auto& bx : items)
    {
        if (!bx.platform.empty() && !placed.count(bx.id))
        {
            platforms.insert(bx.platform);
        }
    }
    return platforms.size();
}

} // namespace

ContainerLoad RgsPacker::pack_single_impl(
    const std::vector<Box>& items,
    const ContainerType& ct,
    const std::vector<Placement>& existing,
    const TenderState& tender,
    bool stop_when_complete)
{
    // rgs_single_uld：单容器多起点搜索，评分 (体积率, 剩余平台数) 字典序
    // 5 种排序策略；route 存在时确定性 pass 统一按深度优先稳定排序（深处平台先放）
    std::vector<rgs::SortCriterion> criteria = {
        rgs::SortCriterion::StackabilityCumulatedVolume,
        rgs::SortCriterion::StackabilityHighestVolume,
        rgs::SortCriterion::CumulatedVolume,
        rgs::SortCriterion::HighestVolume,
        rgs::SortCriterion::Random,
    };
    const int num_criteria = static_cast<int>(criteria.size());
    const int min_per_crit = config::RGS_MIN_TOTAL / num_criteria;
    const int max_per_crit = config::RGS_MAX_TOTAL / num_criteria;

    // 完成判定：load.placements 含已有放置 + 新增，须与总量对齐
    // （否则后处理合并在 existing 非空时把"已放部分捐献箱"误判为完成，导致合并失败）
    const size_t complete_size = existing.size() + items.size();

    // 无 platform 快速路径：items 没有任何平台时 count_remaining_platforms 恒 0，
    // 每迭代直接取 0，避免每迭代构建两个 std::set 全量扫描 placements + items
    bool any_platform = false;
    for (const auto& bx : items)
    {
        if (!bx.platform.empty())
        {
            any_platform = true;
            break;
        }
    }

    // 网格单元尺寸只依赖 items（pack_single 内恒定），一次计算供所有迭代复用
    const int32_t cell_size = rgs::compute_cell_size(items, box_type_map_);

    ContainerLoad best_load;
    double best_score = -1e9;
    size_t best_remaining = std::numeric_limits<size_t>::max();

    // 字典序评分：体积率优先，并列时剩余平台数少者优（对齐 min_platform_split）
    auto better = [](double va, size_t ra, double vb, size_t rb) -> bool
    {
        if (va != vb)
        {
            return va > vb;
        }
        return ra < rb;
    };

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
            load.type = &ct;
            rgs::EpContext ctx;
            ctx.extreme_points.insert({0, 0, 0});
            prefill_load(load, existing, box_map_);
            prefill_ep(ctx, existing);
            rgs::insertion_heuristic(items, ct, box_type_map_, crit, rho, problem_, load, ctx, tender, cell_size);

            if (stop_when_complete && load.placements.size() == complete_size)
            {
                best_load = std::move(load);
                goto done;
            }

            size_t remaining = any_platform ? count_remaining_platforms(items, load) : 0;
            double score = load.volume_rate();
            if (better(score, remaining, best_score, best_remaining))
            {
                best_score = score;
                best_remaining = remaining;
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
            load.type = &ct;
            rgs::EpContext ctx;
            ctx.extreme_points.insert({0, 0, 0});
            prefill_load(load, existing, box_map_);
            prefill_ep(ctx, existing);
            rgs::insertion_heuristic(items, ct, box_type_map_, crit, rho, problem_, load, ctx, tender, cell_size);

            if (stop_when_complete && load.placements.size() == complete_size)
            {
                best_load = std::move(load);
                goto done;
            }

            size_t remaining = any_platform ? count_remaining_platforms(items, load) : 0;
            double score = load.volume_rate();
            if (better(score, remaining, best_score, best_remaining))
            {
                best_score = score;
                best_remaining = remaining;
                best_load = std::move(load);
            }
        }
    }

done:
    best_load.type_id = ct.id;
    best_load.type = &ct;
    return best_load;
}

} // namespace pack3d
