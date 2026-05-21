#pragma once

#include <optional>
#include <string>
#include <vector>

#include "geometry.hpp"
#include "types.hpp"

namespace hypercube
{

// =============================================================
// 约束检查层
// =============================================================

/// 单约束检查结果
struct ConstraintResult
{
    bool ok = true;
    std::optional<Violation> violation;
};

// =============================================================
// 输入预校验 — 在求解器启动前运行
// =============================================================

/// 返回所有输入级别的违规
[[nodiscard]] std::vector<Violation> pre_validate_input(const Problem& problem) noexcept;

// =============================================================
// 放置时检查 — 每次尝试放置时运行
// =============================================================

[[nodiscard]] ConstraintResult check_boundary_constraint(const ContainerLoad& load,
                                                         const Position& pos,
                                                         const OrientedSize& osize) noexcept;

[[nodiscard]] ConstraintResult check_overlap_constraint(
    const ContainerLoad& load,
    const Position& pos, const OrientedSize& osize,
    const std::map<std::string, BoxType>& box_type_map) noexcept;

[[nodiscard]] ConstraintResult check_weight_constraint(const ContainerLoad& load,
                                                       const OrientedSize& osize,
                                                       double box_weight) noexcept;

[[nodiscard]] ConstraintResult check_support_constraint(
    const ContainerLoad& load,
    const Position& pos, const OrientedSize& osize,
    double support_rate,
    const std::map<std::string, BoxType>& box_type_map) noexcept;

/// 路线 X 顺序约束：后装载平台的箱子不能比先装载平台的箱子放得更深（X 更大）
[[nodiscard]] ConstraintResult check_route_order_constraint(
    const ContainerLoad& load,
    const std::string& platform,
    const Position& pos, const OrientedSize& osize,
    const RouteOrder& route) noexcept;

/// 平台数量限制约束（预检）
[[nodiscard]] ConstraintResult check_platform_limit_constraint(
    const ContainerLoad& load,
    const std::string& platform,
    int platform_limit) noexcept;

// =============================================================
// 最终解校验 — 对完整解运行所有硬约束
// =============================================================

[[nodiscard]] std::vector<Violation> final_check_solution(
    const Solution& solution,
    const Problem& problem,
    const std::map<std::string, BoxType>& box_type_map,
    const std::map<std::string, Box>& box_map) noexcept;

} // namespace hypercube
