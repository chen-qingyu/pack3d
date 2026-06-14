#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "constraints.hpp"
#include "geometry.hpp"
#include "objectives.hpp"
#include "types.hpp"

namespace hypercube
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
    std::map<std::string, BoxType> box_type_map;
    std::map<std::string, ContainerType> container_type_map;

    std::vector<ContainerLoad> open_containers;

    // 容器类型使用计数（用于 quantity_limit）
    std::map<std::string, int> container_type_usage;

    int next_container_instance = 0;

    ObjectiveVector current_objective; // 缓存

    // 组分散：group -> 容器实例 ID 集合
    std::map<std::string, std::set<std::string>> group_spread;

    std::optional<Solution> best_feasible;

    bool infeasible = false;
    std::string failure_reason;

    std::chrono::steady_clock::time_point start_time;
    double time_limit = 120.0;

    /// 用户指定的目标键顺序（或默认），比较时只关注这些维度
    std::vector<std::string> objective_keys;

    const AlgorithmConfig* config = nullptr;
    const Problem* problem = nullptr;
};

// SGEP 简单贪心极点算法
class SgepSolver
{
public:
    SgepSolver(
        const Problem& problem,
        const std::map<std::string, BoxType>& box_type_map,
        const std::map<std::string, ContainerType>& container_type_map,
        const std::map<std::string, Box>& box_map,
        bool has_weight_info);

    [[nodiscard]] Solution solve();

private:
    const Problem& problem_;
    const std::map<std::string, BoxType>& box_type_map_;
    const std::map<std::string, ContainerType>& container_type_map_;
    const std::map<std::string, Box>& box_map_;
    bool has_weight_info_;

    SearchState make_initial_state() const;
    bool construct_solution(SearchState& state);
    bool open_new_container(SearchState& state);
    bool place_next_box(SearchState& state);
    void apply_placement(SearchState& state, Candidate& cand);
    bool check_time(const SearchState& state) const;
    void update_best(SearchState& state);
    Solution build_solution(const SearchState& state, bool success,
                            const std::string& reason) const;
    bool check_tender_limit(SearchState& state);
    Position compactify_placement(const ContainerLoad& container,
                                  const Box& box,
                                  Position pos,
                                  const OrientedSize& osize) const;
};

} // namespace hypercube
