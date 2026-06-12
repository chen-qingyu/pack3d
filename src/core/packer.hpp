#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "block.hpp"
#include "types.hpp"

namespace hypercube
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
/// 对一个容器 + 箱子子集，使用简单块 + 空间栈 + 贪心选择进行装载
class ContainerPacker
{
public:
    ContainerPacker(
        const ContainerType& container,
        const std::map<std::string, BoxType>& box_type_map,
        const std::map<std::string, Box>& box_map,
        const Problem& problem,
        bool has_weight_info);

    /// 运行装载，返回结果
    [[nodiscard]] PackResult pack(const std::vector<Box>& boxes);

private:
    const ContainerType& container_;
    const std::map<std::string, BoxType>& box_type_map_;
    const std::map<std::string, Box>& box_map_;
    const Problem& problem_;
    bool has_weight_info_;

    BlockGenerator block_gen_;

    /// 从块表中筛选当前空间可行的候选块
    [[nodiscard]] std::vector<const SimpleBlock*> filter_viable_blocks(
        const std::vector<SimpleBlock>& all_blocks,
        const Space& space,
        const std::map<std::string, int>& available,
        const ContainerLoad& state) const;

    /// 检查块是否满足所有硬约束
    [[nodiscard]] bool check_block_feasible(
        const SimpleBlock& block,
        const Space& space,
        const ContainerLoad& state) const;

    /// 放置块并更新状态
    void place_block(const SimpleBlock& block, const Space& space,
                     ContainerLoad& state,
                     std::map<std::string, int>& available,
                     std::vector<Space>& stack) const;
};

} // namespace hypercube
