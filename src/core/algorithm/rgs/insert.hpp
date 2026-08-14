#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../../constraints.hpp"
#include "../../types.hpp"
#include "order.hpp"
#include "state.hpp"

namespace pack3d::rgs
{

// Alg1: 单 ULD 插入启发式（状态经 out_load / out_ctx 返回）。
// cell_size 由调用方（pack_single）预计算一次，供所有迭代复用网格单元尺寸
void insertion_heuristic(
    const std::vector<Box>& items,
    const ContainerType& ctype,
    const std::map<std::string, BoxType>& box_type_map,
    SortCriterion criterion,
    double rho,
    const Problem& problem,
    ContainerLoad& out_load,
    EpContext& out_ctx,
    const TenderState& tender,
    int32_t cell_size) noexcept;

// 四道门检查（论文 §4.4）
[[nodiscard]] bool can_place(
    const Box& box,
    const BoxType& box_type,
    Orientation orient,
    const Position& ep,
    const ContainerLoad& load,
    const EpContext& ctx,
    const std::map<std::string, BoxType>& box_type_map,
    const Problem& problem) noexcept;

// Alg3: 从新放置的箱子生成新 EP（论文 §4.4.1）。stackable = 新箱是否可被叠放：
// 非可堆叠箱跳过从顶部角点出发的 x/y 投影与顶部中心 EP（论文只对可堆叠箱生成）
[[nodiscard]] std::vector<Position> gen_new_ep(
    const Position& pos,
    const OrientedSize& osize,
    const ContainerLoad& load,
    bool stackable) noexcept;

// Alg4: 投影子过程（论文 §4.5）。order 为 gen_new_ep 预排好的、按方向 d 端位置
// 降序的 placement 下标序列（一次排序复用，避免每次投影重新排序）
[[nodiscard]] std::vector<Position> projection(
    const Position& p,
    int d, // 投影方向: 0=x, 1=y, 2=z
    const ContainerLoad& load,
    const std::vector<size_t>& order) noexcept;

// 提交放置：更新 ContainerLoad 和 EpContext
void commit_placement(
    ContainerLoad& load,
    EpContext& ctx,
    const Box& box,
    const BoxType& box_type,
    Orientation orient,
    const Position& ep,
    const Problem& problem) noexcept;

} // namespace pack3d::rgs
