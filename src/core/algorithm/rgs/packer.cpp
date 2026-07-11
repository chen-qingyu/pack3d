#include "packer.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

#include "../../tool.hpp"
#include "../config.hpp"
#include "insert.hpp"
#include "order.hpp"

namespace pack3d
{

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

} // namespace pack3d
