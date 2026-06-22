#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../../types.hpp"
#include "block.hpp"
#include "space.hpp"

namespace hypercube::mlhs
{

/// 单个容器的装载结果
struct PackResult
{
    bool success = false;
    std::vector<Placement> placements;
    std::vector<std::string> unpacked_box_ids;
    int64_t used_volume = 0;
    double total_weight = 0.0;
    std::set<std::string> platforms;
    std::set<std::string> groups;
    std::map<std::string, int32_t> platform_x_max;
    std::map<std::string, int32_t> platform_x_min;
};

/// 容器内装载引擎（MLHS 块装载法）
/// 对一个容器 + 箱子子集，使用简单块 + 空间栈 + beam 搜索进行装载
class Heuristic
{
public:
    Heuristic(
        const ContainerType& container,
        const std::map<std::string, BoxType>& box_type_map,
        const std::map<std::string, Box>& box_map,
        const Problem& problem,
        bool has_weight_info);

    /// Beam 搜索装载
    /// beam_width: 每层保留的部分方案数（MLHS 论文取 6~16）
    [[nodiscard]] PackResult pack_beam(const std::vector<Box>& boxes, int beam_width);

private:
    struct LocalPackScore
    {
        int platform_split = 0;
        int group_count = 0;
        int64_t used_volume = 0;
        int placed_count = 0;
    };

    const ContainerType& container_;
    const std::map<std::string, BoxType>& box_type_map_;
    const std::map<std::string, Box>& box_map_;
    const Problem& problem_;
    bool has_weight_info_;

    BlockGenerator block_gen_;
    std::map<std::string, double> type_avg_weight_;

    /// 从块表中筛选当前空间可行的候选块
    [[nodiscard]] std::vector<const SimpleBlock*> filter_viable_blocks(
        const std::vector<SimpleBlock>& all_blocks,
        const Space& space,
        const std::map<std::string, std::vector<double>>& available,
        const ContainerLoad& state) const;

    /// 检查块是否满足所有硬约束
    [[nodiscard]] bool check_block_feasible(
        const SimpleBlock& block,
        const Space& space,
        const ContainerLoad& state) const;

    /// 放置块并更新状态
    void place_block(const SimpleBlock& block, const Space& space,
                     ContainerLoad& state,
                     std::map<std::string, std::vector<double>>& available,
                     std::vector<Space>& stack) const;

    /// 从最终状态构建 PackResult
    [[nodiscard]] PackResult make_result(const ContainerLoad& state,
                                         const std::map<std::string, std::vector<double>>& available,
                                         const std::vector<Box>& all_boxes) const;

    [[nodiscard]] LocalPackScore score_state(const ContainerLoad& state) const;

    [[nodiscard]] int compare_local_scores(const LocalPackScore& a,
                                           const LocalPackScore& b) const;

    [[nodiscard]] const SimpleBlock* pick_best_block(
        const std::vector<const SimpleBlock*>& viable,
        const Space& space,
        const ContainerLoad& state,
        const std::map<std::string, std::vector<double>>& available,
        const std::vector<Space>& stack,
        const std::vector<SimpleBlock>& all_blocks,
        int eval_count) const;

    /// 贪心完成：从给定状态开始，不断放置最大可行块直到装不下，返回最终填充率
    /// use_pick_best: 为 true 时对每步候选做前瞻评估（适合大块）；false 时直接取最大块（适合小块）
    [[nodiscard]] LocalPackScore greedy_complete(
        ContainerLoad state,
        std::map<std::string, std::vector<double>> available,
        std::vector<Space> stack,
        const std::vector<SimpleBlock>& all_blocks,
        bool use_pick_best = true) const;

    [[nodiscard]] LocalPackScore complete_largest(
        ContainerLoad state,
        std::map<std::string, std::vector<double>> available,
        std::vector<Space> stack,
        const std::vector<SimpleBlock>& all_blocks) const;
};

} // namespace hypercube::mlhs
