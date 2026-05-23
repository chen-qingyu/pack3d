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

// JSON 反序列化（输入）

/// 将 JSON 字符串解析为 Problem。成功返回 Problem，
/// 失败返回 status=InvalidInput 的 Solution
[[nodiscard]] std::variant<Problem, Solution> parse_json(const std::string& json_text) noexcept;

/// 从 json 对象反序列化 Problem
[[nodiscard]] std::optional<Problem> problem_from_json(const json& j) noexcept;

/// 输入语义校验（schema 无法表达的跨字段校验：重复 ID、引用完整性、路线等）
[[nodiscard]] std::vector<Violation> pre_validate_input(const Problem& problem) noexcept;

// JSON 序列化（输出）

/// 将 Solution 转换为 json 对象（保留插入顺序）
[[nodiscard]] json solution_to_json(const Solution& sol) noexcept;

[[nodiscard]] std::string status_to_string(Status s) noexcept;

} // namespace hypercube
