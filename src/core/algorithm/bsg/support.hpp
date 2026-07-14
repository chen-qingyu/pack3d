#pragma once

#include "types.hpp"

namespace pack3d::bsg
{

bool is_supported(const BSGState& state,
                  const Position& position,
                  const OrientedSize& size,
                  const GlobalContext& ctx) noexcept;

} // namespace pack3d::bsg