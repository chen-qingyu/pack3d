#include "packer.hpp"

#include <algorithm>
#include <set>

#include <spdlog/spdlog.h>

#include "../../tool.hpp"
#include "../config.hpp"
#include "eval.hpp"
#include "insert.hpp"
#include "order.hpp"

namespace pack3d
{

RgsPacker::RgsPacker(
    const Problem& problem,
    const std::map<std::string, BoxType>& box_type_map,
    const std::map<std::string, ContainerType>& container_type_map,
    const std::map<std::string, Box>& box_map,
    bool has_weight_info)
    : PackerBase(problem, box_type_map, box_map, has_weight_info)
    , container_type_map_(container_type_map)
{
}

ContainerLoad RgsPacker::pack_single(
    const std::vector<Box>& items,
    const ContainerType& ct)
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
            rgs::EpContext ctx;
            auto loaded1 = rgs::insertion_heuristic(items, ct, box_type_map_, crit, rho, problem_, load, ctx);
            (void)loaded1;

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
            rgs::EpContext ctx;
            auto loaded2 = rgs::insertion_heuristic(items, ct, box_type_map_, crit, rho, problem_, load, ctx);
            (void)loaded2;

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

void RgsPacker::rebuild_tracking(ContainerLoad& cl) noexcept
{
    cl.platforms.clear();
    cl.groups.clear();
    cl.platform_x_max.clear();
    cl.platform_x_min.clear();
    for (const auto& pl : cl.placements)
    {
        if (!pl.platform.empty())
        {
            cl.platforms.insert(pl.platform);
            int32_t xmax = pl.position.x + pl.osize.dx;
            auto it = cl.platform_x_max.find(pl.platform);
            if (it == cl.platform_x_max.end() || xmax > it->second)
            {
                cl.platform_x_max[pl.platform] = xmax;
            }
            int32_t xmin = pl.position.x;
            auto it2 = cl.platform_x_min.find(pl.platform);
            if (it2 == cl.platform_x_min.end() || xmin < it2->second)
            {
                cl.platform_x_min[pl.platform] = xmin;
            }
        }
        if (!pl.group.empty())
        {
            cl.groups.insert(pl.group);
        }
    }
}

} // namespace pack3d
