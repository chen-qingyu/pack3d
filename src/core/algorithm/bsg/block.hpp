#pragma once

#include <vector>

#include "types.hpp"

namespace hypercube::bsg
{

/// GeneralBlockGeneration (K2)
/// 阶段 1：为每种 box_type + 朝向枚举 simple block
/// 阶段 2：增量合并生成 general block (Fanslau & Bortfeldt 合并法)
///
/// available_counts[type_idx] = 该箱型的原始可用件数
std::vector<GeneralBlock> generate_blocks(
    const Size& container_size,
    const std::vector<BoxType>& box_types,
    const std::vector<int>& available_counts,
    double max_fr,
    int max_bl);

} // namespace hypercube::bsg
