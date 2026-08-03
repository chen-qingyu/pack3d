#pragma once

#include "io.hpp"

namespace pack3d
{

/// 统一入口：接收 json 对象，返回 json 输出对象；异常在内部捕获并返回 invalid
[[nodiscard]] json run(const json& j);

} // namespace pack3d
