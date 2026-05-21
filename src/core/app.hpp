#pragma once

#include <nlohmann/json.hpp>

namespace hypercube
{

// =============================================================
// 统一入口（CLI, report, Python 绑定共用）
// =============================================================

/// 解析 JSON 输入、运行求解器、返回 JSON 输出对象
[[nodiscard]] nlohmann::json run_solver(const std::string& json_input, bool debug = false) noexcept;

} // namespace hypercube
