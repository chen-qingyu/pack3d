#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "types.hpp"

// 项目统一使用 ordered_json 以保留字段插入顺序
using json = nlohmann::ordered_json;

namespace hypercube
{

/// JSON Schema 校验（data/input_schema.json）
[[nodiscard]] std::vector<Violation> validate_schema(const json& j) noexcept;

/// 从 json 对象反序列化 Problem
[[nodiscard]] std::optional<Problem> problem_from_json(const json& j) noexcept;

/// 输入语义校验（schema 无法表达的跨字段校验：重复 ID、引用完整性、路线等）
[[nodiscard]] std::vector<Violation> pre_validate_input(const Problem& problem) noexcept;

/// 将 Solution 转换为 json 对象（保留插入顺序）
[[nodiscard]] json solution_to_json(const Solution& sol) noexcept;

} // namespace hypercube
