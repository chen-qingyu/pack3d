#pragma once

#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "types.hpp"

namespace pack3d
{

/// 检查箱子是否完全在容器边界内
[[nodiscard]] bool check_boundary(const ContainerType& ctype, const Position& pos,
                                  const OrientedSize& osize) noexcept;

/// 检查新放置是否与已有放置重叠
[[nodiscard]] bool check_overlap(const Position& pos, const OrientedSize& osize,
                                 const std::vector<Placement>& existing) noexcept;

/// 检查放置是否与容器障碍物相交（面贴面允许，相交禁止）
[[nodiscard]] bool check_obstacle(const Position& pos, const OrientedSize& osize,
                                  const std::vector<Obstacle>& obstacles) noexcept;

/// 障碍物 8 角点（4 顶角 + 4 底角），极点类算法的候选点种子
[[nodiscard]] std::array<Position, 8> obstacle_corners(const Obstacle& o) noexcept;

/// 检查放置是否与斜面禁区相交（楔形凸半空间：箱子极角点单次点积判定，面贴面允许）
[[nodiscard]] bool check_facet(const Position& pos, const OrientedSize& osize,
                               const Size& container_size,
                               const std::vector<Facet>& facets) noexcept;

/// 单个斜面禁区是否覆盖原点 (0,0,0)：两截距均为负（从 min 侧切入，贴原点角）
[[nodiscard]] bool facet_covers_origin(const Facet& f) noexcept;

/// 是否存在覆盖原点的斜面（有则算法需备用起始点/雕刻贴角楔形，否则原点被禁会零装载）
[[nodiscard]] bool facet_covers_origin(const std::vector<Facet>& facets) noexcept;

/// 原点被斜面覆盖时，返回地板 (z=0) 上第一个能放下 ref（不侵入禁区）的可用起点
[[nodiscard]] std::optional<Position> first_floor_spot(const Size& csize,
                                                       const std::vector<Facet>& facets,
                                                       const OrientedSize& ref) noexcept;

// 斜面楔形的 N 步阶梯 AABB 近似（覆盖整个楔形禁区，过挖≈楔形/N，远小于 AABB 的楔形过挖）
struct FacetSlab
{
    int32_t x = 0, y = 0, z = 0;
    int32_t dx = 0, dy = 0, dz = 0;
};
[[nodiscard]] std::vector<FacetSlab> facet_staircase(const Facet& f,
                                                     const Size& csize,
                                                     int steps) noexcept;

// 斜面楔形阶梯近似的步数（仅用于覆盖原点的斜面，GLC/BSG 共用）
inline constexpr int FACET_STAIR_STEPS = 2;

/// 检查放入箱子后是否超重
[[nodiscard]] bool check_weight(const ContainerLoad& load,
                                double box_weight) noexcept;

/// 检查底面支撑率是否达标（support_rate=0 则跳过）
[[nodiscard]] bool check_support(const Position& pos, const OrientedSize& osize,
                                 const ContainerLoad& load,
                                 double support_rate) noexcept;

/// 堆码层数/单箱承重只读预检（max_stack + max_load，逐直接支撑箱判定）
[[nodiscard]] bool check_stack_constraints(
    const Position& pos, const OrientedSize& osize, double weight,
    const ContainerLoad& load,
    const std::map<std::string, BoxType>& box_type_map) noexcept;

/// 放置提交后的堆叠状态副作用：新箱（load.placements.back()）的
/// stack_level/supported_load，及直接支撑箱的 supported_load 增量。
void apply_stack_state(const Position& pos, const OrientedSize& osize, double weight,
                       ContainerLoad& load) noexcept;

/// 按 z 排序重建全部放置的堆叠状态；errors 非空时同时校验 max_stack/max_load。
/// 用于 resume、后处理合并、预校验等任意顺序构造的装载。
void recompute_stack_state(ContainerLoad& load,
                           const std::map<std::string, BoxType>& box_type_map,
                           std::vector<std::string>* errors = nullptr) noexcept;

/// 平台数量限制预检
[[nodiscard]] bool check_platform_limit(const ContainerLoad& load,
                                        const std::string& platform,
                                        int platform_limit) noexcept;

/// 路线顺序约束：卸货顺序中靠前的平台（idx 小，先卸）应在更近门处（X 大），
/// 靠后的平台（后卸）应在更深处（X 小）
[[nodiscard]] bool check_route_order(const ContainerLoad& load,
                                     const std::string& platform,
                                     const Position& pos, const OrientedSize& osize,
                                     const RouteOrder& route) noexcept;

// ============================================================
// tender（发标）约束：容器按共享 group 连通，每个连通分量即一个 tender。
// tender_limit 限制每个 tender 最多包含的容器数。
// ============================================================

/// 已提交容器的 tender 分解（单次 pack_single 内不可变）
struct TenderState
{
    int limit = 0;                                         // 0 = 未启用
    std::vector<int> sizes;                                // tender id -> 容器数
    std::map<std::string, std::vector<int>> group_tenders; // group -> 含该 group 的已提交 tender id（升序去重）
};

/// 构建已提交容器的 tender 分解；current 为正在装载的容器下标（自身不计入已提交），
/// current >= all_loads.size() 表示开新容器（全部已提交）。limit<=0 返回未启用状态。
[[nodiscard]] TenderState build_tender_state(const std::vector<ContainerLoad>& all_loads,
                                             size_t current, int tender_limit);

/// 预检：把 group 加入"已含 groups 的当前容器"是否会使所属 tender 超过 limit。
/// group 已在 groups 中恒真；limit<=0 或 group 为空恒真。
[[nodiscard]] bool check_tender_limit(const TenderState& ts,
                                      const std::set<std::string>& groups,
                                      const std::string& group) noexcept;

/// 校验全部容器的每个 tender 连通分量都不超过 limit（后处理候选整体复检用）
[[nodiscard]] bool check_all_tenders(const std::vector<ContainerLoad>& all_loads,
                                     int tender_limit) noexcept;

/// 全部容器的 tender 序号：连通分量按容器顺序首次出现编号（1-based），无 group 为 null。
std::vector<std::optional<int>> compute_container_tenders(
    const std::vector<ContainerLoad>& all_loads);

} // namespace pack3d
