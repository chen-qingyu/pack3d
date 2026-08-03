#include "packer.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

#include "../../tool.hpp"
#include "../config.hpp"
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

} // namespace

ContainerLoad RgsPacker::pack_single(
    const std::vector<Box>& items,
    const ContainerType& ct,
    const std::vector<Placement>& existing,
    bool stop_when_complete)
{
    // rgs_single_uld：单容器多起点搜索，评分用纯体积率
    static const rgs::SortCriterion criteria[] = {
        rgs::SortCriterion::StackabilityCumulatedVolume,
        rgs::SortCriterion::StackabilityHighestVolume,
        rgs::SortCriterion::CumulatedVolume,
        rgs::SortCriterion::HighestVolume,
        rgs::SortCriterion::Random,
    };
    constexpr int num_criteria = 5;
    constexpr int min_per_crit = config::RGS_MIN_TOTAL / num_criteria;
    constexpr int max_per_crit = config::RGS_MAX_TOTAL / num_criteria;

    // 完成判定：load.placements 含已有放置 + 新增，须与总量对齐
    // （否则后处理合并在 existing 非空时把"已放部分捐献箱"误判为完成，导致合并失败）
    const size_t complete_size = existing.size() + items.size();

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
            load.type = &ct;
            rgs::EpContext ctx;
            ctx.extreme_points.insert({0, 0, 0});
            prefill_load(load, existing, box_map_);
            prefill_ep(ctx, existing);
            rgs::insertion_heuristic(items, ct, box_type_map_, crit, rho, problem_, load, ctx);

            if (stop_when_complete && load.placements.size() == complete_size)
            {
                best_load = std::move(load);
                goto done;
            }

            double score = load.volume_rate();
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
            load.type = &ct;
            rgs::EpContext ctx;
            ctx.extreme_points.insert({0, 0, 0});
            prefill_load(load, existing, box_map_);
            prefill_ep(ctx, existing);
            rgs::insertion_heuristic(items, ct, box_type_map_, crit, rho, problem_, load, ctx);

            if (stop_when_complete && load.placements.size() == complete_size)
            {
                best_load = std::move(load);
                goto done;
            }

            double score = load.volume_rate();
            if (score > best_score)
            {
                best_score = score;
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
