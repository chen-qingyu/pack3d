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

// 用已有放置预填充 load 和 EP 上下文
void prefill_load_and_ep(
    ContainerLoad& load,
    rgs::EpContext& ctx,
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
            prefill_load_and_ep(load, ctx, existing, box_map_);
            auto loaded1 = rgs::insertion_heuristic(items, ct, box_type_map_, crit, rho, problem_, load, ctx);
            (void)loaded1;

            if (stop_when_complete && load.placements.size() == items.size())
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
            prefill_load_and_ep(load, ctx, existing, box_map_);
            auto loaded2 = rgs::insertion_heuristic(items, ct, box_type_map_, crit, rho, problem_, load, ctx);
            (void)loaded2;

            if (stop_when_complete && load.placements.size() == items.size())
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
