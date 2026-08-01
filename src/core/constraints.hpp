#pragma once

#include <map>
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

/// 路线顺序约束：先装平台（idx 小）应在更深处（X 小），后装平台应在更近门处（X 大）
[[nodiscard]] bool check_route_order(const ContainerLoad& load,
                                     const std::string& platform,
                                     const Position& pos, const OrientedSize& osize,
                                     const RouteOrder& route) noexcept;

} // namespace pack3d
