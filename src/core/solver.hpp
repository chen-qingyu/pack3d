#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "constraints.hpp"
#include "geometry.hpp"
#include "objectives.hpp"
#include "types.hpp"

namespace hypercube
{

// =============================================================
// 求解器评估的候选放置
// =============================================================
struct Candidate
{
    std::string box_id;
    std::string container_instance_id;
    Position position;
    Orientation orientation = Orientation::XYZ;
    OrientedSize osize;
};

// =============================================================
// 搜索状态 — 求解过程中可变
// =============================================================
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

    bool proven_infeasible = false;
    std::string failure_reason;

    std::chrono::steady_clock::time_point start_time;
    double time_limit_seconds = 120.0;

    /// 用户指定的目标键顺序（或默认），比较时只关注这些维度
    std::vector<std::string> objective_keys;

    const SolverConfig* config = nullptr;
    const Problem* problem = nullptr;
};

// =============================================================
// 箱子排序策略（用于多起点）
// =============================================================
enum class BoxOrder : uint8_t
{
    ByVolume,             // 大体积优先（默认）
    ByVolumeAsc,          // 小体积优先
    ByHeight,             // 高箱子优先（Y 方向最受限）
    ByPlatformThenVolume, // 按路线平台分组，再按体积
    ByMixed,              // 打乱（增加多样性）
};

// =============================================================
// 求解器引擎
// =============================================================
class SolverEngine
{
public:
    explicit SolverEngine(const Problem& problem);

    /// 运行求解器，返回最佳解
    [[nodiscard]] Solution solve();

private:
    Problem problem_;
    std::map<std::string, BoxType> box_type_map_;
    std::map<std::string, ContainerType> container_type_map_;
    std::map<std::string, Box> box_map_;

    // --- 内部辅助 ---

    SearchState make_initial_state(BoxOrder order = BoxOrder::ByVolume) const;

    bool construct_solution(SearchState& state);
    bool open_new_container(SearchState& state);
    bool place_next_box(SearchState& state);

    void apply_placement(SearchState& state, Candidate& cand);
    bool check_time(const SearchState& state) const;
    void update_best(SearchState& state);

    Solution build_solution(const SearchState& state, Status status,
                            const std::string& reason) const;

    void multi_start_solve(SearchState& state);

    bool check_tender_limit_proven_infeasible(SearchState& state);

    Position compactify_placement(const ContainerLoad& container,
                                  const Box& box,
                                  Position pos, const OrientedSize& osize) const;
};

} // namespace hypercube
