#pragma once

#include <optional>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

#include "types.hpp"

namespace hypercube
{

// 项目统一使用 ordered_json 以保留字段插入顺序
using json = nlohmann::ordered_json;

// =============================================================
// JSON 反序列化（输入）
// =============================================================

/// 将 JSON 字符串解析为 Problem。成功返回 Problem，
/// 失败返回 status=InvalidInput 的 Solution
[[nodiscard]] std::variant<Problem, Solution> parse_json(const std::string& json_text) noexcept;

/// 从 json 对象反序列化 Problem
/// 不执行语义校验（需单独调用 pre_validate_input）
[[nodiscard]] std::optional<Problem> problem_from_json(const json& j) noexcept;

// =============================================================
// JSON 序列化（输出）
// =============================================================

/// 将 Solution 转换为 json 对象（保留插入顺序）
[[nodiscard]] json solution_to_json(const Solution& sol) noexcept;

/// 将 Solution 序列化为美化打印的 JSON 字符串
[[nodiscard]] std::string solution_to_json_string(const Solution& sol, int indent = 2) noexcept;

// =============================================================
// Status / reason 与字符串互转
// =============================================================
[[nodiscard]] std::string_view status_to_string(Status s) noexcept;
[[nodiscard]] std::optional<Status> status_from_string(const std::string& s) noexcept;

} // namespace hypercube
