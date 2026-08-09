#pragma once

#include <vector>

#include "types.hpp"

namespace pack3d
{

class PackerBase;

/// 装托循环：把散件（palletize:true 箱型的箱子）装入托盘。
/// 同组不拆托（软）：组内 >= 2 箱且整组装得下 → 该组独占一托；否则混合兜底。
/// route 启用时按 (platform, group) 分组（整托只能去一个卸货点）。
/// 装不进的散件收集到 unpalletized，由调用方按 pallet_fallback 决定降级散装或保持未装箱。
[[nodiscard]] std::vector<PalletLoad> palletize(
    const Problem& problem,
    PackerBase& packer,
    std::vector<Box>& unpalletized);

/// 问题改写：每个托盘 → 一个虚拟装箱单元（BoxType + Box）；
/// 散件替换为虚拟箱，普通箱子原样保留；pallet_fallback=true 时未装托散件降级散装回到 boxes。
/// 注意：会重建 has_max_stack/has_max_load（虚拟托盘箱型自带 max_stack）。
[[nodiscard]] Problem transform_pallet(
    const Problem& problem,
    const std::vector<PalletLoad>& pallet_loads,
    const std::vector<Box>& unpalletized,
    bool pallet_fallback);

/// 输出展开：solution 增加 pallets 明细，packed_box_count/container packed_count 折算原始散箱口径，
/// 补充 summary 的 pallet_count/palletized_box_count/loose_box_count；
/// fallback=false 时未装托散件补记未装箱（partial）并给出 violation。
void expand_pallet_solution(Solution& sol,
                            const std::vector<PalletLoad>& pallet_loads,
                            const std::vector<Box>& unpalletized,
                            bool pallet_fallback) noexcept;

} // namespace pack3d
