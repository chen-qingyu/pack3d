#pragma once

#include "io.hpp"

namespace pack3d
{

/// 统一入口：接收 json 对象，返回 json 输出对象
[[nodiscard]] json run(const json& j) noexcept;

} // namespace pack3d
