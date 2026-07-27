#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "types.hpp"

// 项目统一使用 json 以支持自动转换为 Python dict
using json = nlohmann::json;

namespace pack3d
{

/// JSON Schema 校验（编译时嵌入 input_schema.h）
[[nodiscard]] std::vector<std::string> validate_schema(const json& j) noexcept;

/// 输入语义校验（schema 无法表达的跨字段校验：重复 ID、引用完整性、路线等）
[[nodiscard]] std::vector<std::string> pre_validate_input(const Problem& problem) noexcept;

// enum 字符串转换
[[nodiscard]] std::string algorithm_to_string(Algorithm a) noexcept;
[[nodiscard]] Algorithm algorithm_from_string(const std::string& s) noexcept;
[[nodiscard]] std::string status_to_string(SolveStatus s) noexcept;

// nlohmann/json ADL 序列化/反序列化
void from_json(const json& j, ContainerType& ct);
void from_json(const json& j, BoxType& bt);
void from_json(const json& j, Box& bx);
void from_json(const json& j, ExistingPlacement& ep);
void from_json(const json& j, ExistingContainer& ec);
void from_json(const json& j, Problem& p);
void to_json(json& j, const Solution& sol);

/// 从已有放置构建 ContainerLoad（校验用 + pack 预填充）
[[nodiscard]] ContainerLoad build_load_from_existing(
    const ExistingContainer& ec,
    const std::map<std::string, ContainerType>& ct_map,
    const std::map<std::string, BoxType>& bt_map,
    std::vector<std::string>& errors);

} // namespace pack3d
