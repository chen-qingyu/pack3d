#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "types.hpp"

namespace pack3d
{

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

    virtual ContainerLoad pack_single(
        const std::vector<Box>& items,
        const ContainerType& ct,
        const std::vector<Placement>& existing,
        bool stop_when_complete = false) = 0;

    Solution pack();

    double support_rate() const
    {
        return problem_.support_rate;
    }

    bool has_max_stack() const
    {
        return problem_.has_max_stack;
    }

    bool has_max_load() const
    {
        return problem_.has_max_load;
    }

protected:
    const Problem& problem_;
    const std::map<std::string, BoxType>& box_type_map_;
    const std::map<std::string, Box>& box_map_;
    bool has_weight_info_;

private:
    Solution build_solution(
        const std::vector<ContainerLoad>& all_loads,
        const std::vector<Box>& remaining);
};

/// 用已有放置预填充 load 的 placements 与聚合字段（used_volume/platforms/groups/total_weight）
void prefill_load(ContainerLoad& load,
                  const std::vector<Placement>& existing,
                  const std::map<std::string, Box>& box_map);

} // namespace pack3d
