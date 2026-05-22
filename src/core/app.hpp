#pragma once

#include <string>

#include "io.hpp"

namespace hypercube
{

/// 解析 JSON 输入、运行求解器、返回 JSON 输出对象
[[nodiscard]] json run_solver(const std::string& json_input, bool debug = false) noexcept;

} // namespace hypercube
