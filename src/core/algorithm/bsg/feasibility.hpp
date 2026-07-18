#pragma once

#include "types.hpp"

namespace pack3d::bsg
{

[[nodiscard]] bool can_place_block(
    const BSGState& state,
    int block_idx,
    const Position& position,
    const GlobalContext& ctx,
    ContainerLoad& next_load,
    std::vector<int>& next_item_classes) noexcept;

} // namespace pack3d::bsg