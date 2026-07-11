#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "algorithm/select_container.hpp"
#include "objectives.hpp"
#include "postprocess.hpp"
#include "tool.hpp"
#include "types.hpp"

namespace pack3d
{

// 单容器填充接口：输入箱子 + 容器类型，输出装满的容器
class PackerBase
{
public:
    PackerBase(
        const Problem& problem,
        const std::map<std::string, BoxType>& box_type_map,
        const std::map<std::string, Box>& box_map,
        bool has_weight_info)
        : problem_(problem)
        , box_type_map_(box_type_map)
        , box_map_(box_map)
        , has_weight_info_(has_weight_info)
    {
    }

    virtual ~PackerBase() = default;

    // 子类实现：单容器填充
    virtual ContainerLoad pack_single(
        const std::vector<Box>& items,
        const ContainerType& ct) = 0;

    // 模板方法：统一主循环
    Solution pack()
    {
        TimeChecker::init(problem_.time_limit);

        std::vector<ContainerLoad> all_loads;
        std::set<std::string> remaining_ids;
        for (const auto& bx : problem_.boxes)
        {
            remaining_ids.insert(bx.id);
        }

        std::map<std::string, int> container_usage;
        int instance_counter = 0;

        while (!remaining_ids.empty() && TimeChecker::check())
        {
            // 收集剩余箱子
            std::vector<Box> remaining;
            for (const auto& bx : problem_.boxes)
            {
                if (remaining_ids.count(bx.id))
                {
                    remaining.push_back(bx);
                }
            }

            // 统一选车：大优先
            const ContainerType* ct = select_largest_fitting(
                problem_.container_types, container_usage, remaining, box_type_map_);
            if (!ct)
            {
                break;
            }

            // 算法填充单容器
            ContainerLoad load = pack_single(remaining, *ct);
            load.instance_id = ct->id + "_" + std::to_string(instance_counter++);

            // 收箱
            std::set<std::string> packed;
            for (const auto& pl : load.placements)
            {
                packed.insert(pl.box_id);
            }
            for (const auto& id : packed)
            {
                remaining_ids.erase(id);
            }

            container_usage[ct->id]++;

            spdlog::info("Container#{} \"{}\": packed {}, left {}, volume rate: {:.4f}",
                         all_loads.size() + 1, ct->id, load.placements.size(),
                         remaining_ids.size(), load.volume_rate());

            all_loads.push_back(std::move(load));
        }

        // 后处理：重装缩小 + 尾柜降级 + 合并分散平台
        postprocess(all_loads, [this](const std::vector<Box>& items, const ContainerType& ct)
                    { return pack_single(items, ct); }, problem_.container_types, box_type_map_, box_map_);

        return build_solution(all_loads, remaining_ids);
    }

protected:
    const Problem& problem_;
    const std::map<std::string, BoxType>& box_type_map_;
    const std::map<std::string, Box>& box_map_;
    bool has_weight_info_;

private:
    Solution build_solution(
        const std::vector<ContainerLoad>& all_loads,
        const std::set<std::string>& remaining_ids)
    {
        Solution sol;
        sol.elapsed_second = TimeChecker::elapsed();

        int packed = 0;
        for (const auto& cl : all_loads)
        {
            packed += static_cast<int>(cl.placements.size());
        }
        sol.packed_box_count = packed;
        int total = static_cast<int>(problem_.boxes.size());
        sol.unpacked_box_count = total - packed;

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

        for (const auto& cl : all_loads)
        {
            ContainerSummary cs;
            cs.type_id = cl.type->id;
            cs.inner_size = cl.type->inner_size;
            cs.max_weight = cl.type->max_weight;
            cs.used_volume = cl.used_volume;
            cs.volume_rate = cl.volume_rate();
            cs.packed_count = static_cast<int>(cl.placements.size());
            if (has_weight_info_)
            {
                cs.used_weight = cl.total_weight;
                if (cl.type->max_weight.has_value())
                {
                    cs.weight_rate = cl.total_weight / cl.type->max_weight.value();
                }
            }
            cs.platforms = std::vector<std::string>(cl.platforms.begin(), cl.platforms.end());
            cs.groups = std::vector<std::string>(cl.groups.begin(), cl.groups.end());
            sol.container_summaries.push_back(std::move(cs));
        }

        return sol;
    }
};

} // namespace pack3d
