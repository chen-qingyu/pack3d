#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "packer.hpp"
#include "types.hpp"

namespace hypercube
{

/// 全局调度器：多容器分配 + 局部搜索
/// 负责将箱子分配到各个容器，调用 ContainerPacker 验证可行性，
/// 并通过局部搜索改进分配方案
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

    /// 一个容器的分配状态（分配了哪些箱子 + 装载结果）
    struct ContainerSlot
    {
        std::string instance_id;
        const ContainerType* type = nullptr;
        std::vector<std::string> box_ids;      // 分配到本容器的箱子 ID
        std::optional<PackResult> pack_result; // 装载结果（成功或失败）
    };

    /// 完整解：一组容器分配
    struct Assignment
    {
        std::vector<ContainerSlot> slots;
        ObjectiveVector objective;

        bool is_better_than(const Assignment& rhs) const noexcept;
    };

    /// 贪婪初始分配
    [[nodiscard]] Assignment greedy_assign(const std::vector<Box>& boxes);

    /// 局部搜索改进
    void local_search(Assignment& assign, const std::vector<Box>& all_boxes);

    /// 对所有容器运行 ContainerPacker，更新 pack_result
    void repack_all(Assignment& assign);

    /// 对单个容器运行 ContainerPacker
    [[nodiscard]] std::optional<PackResult> pack_container(
        const ContainerType* ct,
        const std::vector<const Box*>& boxes) const;

    /// 计算分配方案的目标向量
    [[nodiscard]] ObjectiveVector compute_objective(const Assignment& assign) const noexcept;

    /// 将 Assignment 转换为 Solution
    [[nodiscard]] Solution to_solution(const Assignment& assign,
                                       const std::vector<Box>& all_boxes,
                                       bool success,
                                       const std::string& reason) const;

    /// 时间检查
    bool check_time() const;
};

} // namespace hypercube
