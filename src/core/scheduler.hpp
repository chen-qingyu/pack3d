#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "packer.hpp"
#include "types.hpp"

namespace hypercube
{

/// 全局调度器：将箱子分配到各个容器，调用 ContainerPacker 进行装载
class GlobalScheduler
{
public:
    GlobalScheduler(
        const Problem& problem,
        const std::map<std::string, BoxType>& box_type_map,
        const std::map<std::string, Box>& box_map,
        bool has_weight_info);

    /// 运行调度，返回最终解
    [[nodiscard]] Solution schedule();

private:
    const Problem& problem_;
    const std::map<std::string, BoxType>& box_type_map_;
    const std::map<std::string, Box>& box_map_;
    bool has_weight_info_;

    std::chrono::steady_clock::time_point start_time_;
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
                                       bool success,
                                       const std::string& reason) const;

    bool check_time() const;
};

} // namespace hypercube
