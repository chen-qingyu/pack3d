#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../../types.hpp"
#include "order.hpp"
#include "state.hpp"

namespace hypercube::rgs
{

// Alg1: 单 ULD 插入启发式
// 返回已装载的箱子 ID 列表
[[nodiscard]] std::vector<std::string> insertion_heuristic(
    const std::vector<Box>& items,
    const ContainerType& ctype,
    const std::map<std::string, BoxType>& box_type_map,
    SortCriterion criterion,
    double rho,
    const Problem& problem,
    ContainerLoad& out_load,
    EpContext& out_ctx) noexcept;

// 四道门检查（论文 §4.4）
[[nodiscard]] bool can_place(
    const Box& box,
    const BoxType& box_type,
    Orientation orient,
    const Position& ep,
    const ContainerLoad& load,
    const EpContext& ctx,
    const std::map<std::string, BoxType>& box_type_map,
    double support_rate,
    const std::optional<RouteOrder>& route,
    const std::optional<int>& platform_limit) noexcept;

// Alg3: 从新放置的箱子生成新 EP（论文 §4.4.1）
[[nodiscard]] std::vector<Position> gen_new_ep(
    const Position& pos,
    const OrientedSize& osize,
    bool stackable,
    const ContainerLoad& load,
    const ContainerType& ctype) noexcept;

// Alg4: 投影子过程（论文 §4.5）
[[nodiscard]] std::vector<Position> projection(
    const Position& p,
    int d, // 投影方向: 0=x, 1=y, 2=z
    const ContainerLoad& load,
    const ContainerType& ctype) noexcept;

// 提交放置：更新 ContainerLoad 和 EpContext
void commit_placement(
    ContainerLoad& load,
    EpContext& ctx,
    const Box& box,
    const BoxType& box_type,
    Orientation orient,
    const Position& ep) noexcept;

} // namespace hypercube::rgs
