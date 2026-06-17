#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "heuristic.hpp"

#include "../../types.hpp"

namespace hypercube::mlhs
{

/// MLHS 入口：将箱子分配到各个容器，调用 Heuristic 进行装载
class Packer
{
public:
    Packer(
        const Problem& problem,
        const std::map<std::string, BoxType>& box_type_map,
        const std::map<std::string, Box>& box_map,
        bool has_weight_info);

    /// 运行调度，返回最终解
    [[nodiscard]] Solution pack();

private:
    const Problem& problem_;
    const std::map<std::string, BoxType>& box_type_map_;
    const std::map<std::string, Box>& box_map_;
    bool has_weight_info_;

    int next_instance_ = 0;
    std::map<std::string, int> container_type_usage_;

    struct ContainerSlot
    {
        std::string instance_id;
        const ContainerType* type = nullptr;
        std::optional<PackResult> pack_result;
    };

    [[nodiscard]] std::optional<PackResult> pack_container(
        const ContainerType* ct,
        const std::vector<const Box*>& boxes) const;

    [[nodiscard]] Solution to_solution(const std::vector<ContainerSlot>& slots,
                                       const std::vector<Box>& all_boxes,
                                       const std::string& status) const;

    /// 处理已达 tender_limit 的组：将剩余箱子装入已有容器
    /// 返回 1 = 已处理（调用方应跳过开新容器），0 = 无需处理，-1 = 不可行
    [[nodiscard]] int handle_tender_limit_groups(
        std::set<std::string>& remaining_ids,
        std::vector<ContainerSlot>& slots);
};

} // namespace hypercube::mlhs
