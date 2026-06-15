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
[[nodiscard]] std::vector<std::string> validate_schema(const json& j) noexcept;

/// 输入语义校验（schema 无法表达的跨字段校验：重复 ID、引用完整性、路线等）
[[nodiscard]] std::vector<std::string> pre_validate_input(const Problem& problem) noexcept;

// nlohmann/json ADL 序列化/反序列化
void from_json(const json& j, ContainerType& ct);
void from_json(const json& j, BoxType& bt);
void from_json(const json& j, Box& bx);
void from_json(const json& j, Problem& p);
void to_json(json& j, const Solution& sol);

} // namespace hypercube
