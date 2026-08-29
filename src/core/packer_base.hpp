#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "constraints.hpp"
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

    /// 单容器填充入口：route 存在时按装货顺序（route 深度降序）**逐平台分桶**放置，
    /// 每桶只允许一个平台的箱子（严格分阶段，最深平台先放）；无 route 时直接委托底层。
    ContainerLoad pack_single(
        const std::vector<Box>& items,
        const ContainerType& ct,
        const std::vector<Placement>& existing,
        const TenderState& tender,
        bool stop_when_complete = false);

    Solution pack();

    double support_rate() const
    {
        return problem_.support_rate;
    }

    int tender_limit() const
    {
        return problem_.tender_limit.value_or(0);
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
    /// 底层单容器填充（单次调用，不跨平台分桶）；各算法实现。
    virtual ContainerLoad pack_single_impl(
        const std::vector<Box>& items,
        const ContainerType& ct,
        const std::vector<Placement>& existing,
        const TenderState& tender,
        bool stop_when_complete) = 0;

    /// 算法自身是否已按 route 深度排序平台（如 RGS 的 build_ordered_list 深度优先）。
    /// true = 不需要 Packer 层的逐平台分桶封装（避免重复/破坏其时间预算迭代搜索）。
    virtual bool self_orders_platforms() const
    {
        return false;
    }

    const Problem& problem_;
    const std::map<std::string, BoxType>& box_type_map_;
    const std::map<std::string, Box>& box_map_;
    bool has_weight_info_;

private:
    Solution build_solution(
        const std::vector<ContainerLoad>& all_loads,
        const std::vector<Box>& remaining);

    /// 危险品分柜编排：危险品优先、单独装柜、仅最后一车混装普货、再装普货。
    void pack_with_danger(
        std::vector<ContainerLoad>& all_loads,
        std::vector<Box>& remaining,
        std::map<std::string, int>& container_usage,
        int& instance_counter,
        const std::map<std::string, const ContainerType*>& ct_map);
};

/// 用已有放置预填充 load 的 placements 与聚合字段（used_volume/platforms/groups/total_weight）
void prefill_load(ContainerLoad& load,
                  const std::vector<Placement>& existing,
                  const std::map<std::string, Box>& box_map);

} // namespace pack3d
