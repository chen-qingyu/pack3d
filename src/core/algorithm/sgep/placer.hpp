#pragma once

#include <map>
#include <string>

#include "../../types.hpp"
#include "state.hpp"

namespace hypercube::sgep
{

/// 单容器极点搜索：遍历已有容器极点和新容器原点，按目标投影选最优放置
class Placer
{
public:
    Placer(const std::map<std::string, BoxType>& box_type_map,
           const Problem& problem,
           bool has_weight_info);

    /// 选最优位置放置一个箱子，返回 true 表示已从 remaining_boxes 移除
    bool place_next_box(SearchState& state);

    /// 执行放置：更新容器状态、极点、目标缓存
    void apply_placement(SearchState& state, Candidate& cand);

private:
    const std::map<std::string, BoxType>& box_type_map_;
    const Problem& problem_;
    bool has_weight_info_;
};

} // namespace hypercube::sgep
