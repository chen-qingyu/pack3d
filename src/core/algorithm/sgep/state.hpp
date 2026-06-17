#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../../objectives.hpp"
#include "../../types.hpp"

namespace hypercube::sgep
{

// 求解器评估的候选放置
struct Candidate
{
    std::string box_id;
    std::string container_instance_id;
    Position position;
    Orientation orientation = Orientation::XYZ;
    OrientedSize osize;
};

// 搜索状态
struct SearchState
{
    std::vector<Box> remaining_boxes;
    std::map<std::string, ContainerType> container_type_map;

    std::vector<ContainerLoad> open_containers;

    // 容器类型使用计数（用于 quantity_limit）
    std::map<std::string, int> container_type_usage;

    int next_container_instance = 0;

    ObjectiveVector current_objective; // 缓存

    // 组分散：group -> 容器实例 ID 集合
    std::map<std::string, std::set<std::string>> group_spread;

    // 各容器的候选极点列表（instance_id -> 极点）
    std::map<std::string, std::vector<Position>> extreme_points;

    std::optional<Solution> best_feasible;

    bool infeasible = false;

    std::chrono::steady_clock::time_point start_time;
    double time_limit = 120.0;

    /// 用户指定的目标键顺序（或默认），比较时只关注这些维度
    std::vector<std::string> objective_keys;
};

} // namespace hypercube::sgep
