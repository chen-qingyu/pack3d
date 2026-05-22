#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace hypercube
{

// 朝向
enum class Orientation : uint8_t
{
    XYZ,
    XZY,
    YXZ,
    YZX,
    ZXY,
    ZYX,
};

/// 有效朝向数量
inline constexpr int k_orientation_count = 6;

// 几何基础类型
struct Size
{
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    int64_t volume() const noexcept
    {
        return static_cast<int64_t>(x) * y * z;
    }
};

struct Position
{
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
};

/// 应用朝向后的实际尺寸
struct OrientedSize
{
    int32_t dx = 0;
    int32_t dy = 0;
    int32_t dz = 0;

    int64_t volume() const noexcept
    {
        return static_cast<int64_t>(dx) * dy * dz;
    }
};

struct ContainerType
{
    std::string id;
    Size inner_size;
    double max_weight = 0.0;
    std::optional<int> quantity_limit; // null 表示无限制
};

struct BoxType
{
    std::string id;
    Size size;
    std::vector<Orientation> allowed_orientations;
};

struct Box
{
    std::string id;
    std::string box_type_id;
    double weight = 0.0;
    std::string group;    // 空字符串表示未设置
    std::string platform; // 空字符串表示未设置
};

// 路线
struct RouteOrder
{
    std::vector<std::string> platform_order;
    std::map<std::string, size_t> index_of; // platform -> 在顺序中的位置
};

// 求解器配置
struct SolverConfig
{
    std::string strategy = "constructive_multi_active";
    int active_container_limit = 3;
    int random_seed = 42;
};

// 完整问题描述
struct Problem
{
    std::vector<ContainerType> container_types;
    std::vector<BoxType> box_types;
    std::vector<Box> boxes;

    // 约束
    double time_limit_seconds = 120.0;
    double support_rate = 0.0;
    std::optional<int> platform_limit;
    std::optional<int> tender_limit;
    std::optional<RouteOrder> route;

    // 目标 — 有序列表；空列表则使用默认值
    std::vector<std::string> objective_keys;

    // 求解器配置
    SolverConfig solver_config;
};

// 放置结果（内部 + 输出）
struct Placement
{
    std::string box_id;
    std::string box_type_id;
    std::string container_id;
    Position position;
    Orientation orientation = Orientation::XYZ;
};

// 容器装载（可变求解状态）
struct ContainerLoad
{
    std::string instance_id; // 每个已打开的容器唯一标识
    std::string type_id;
    const ContainerType* type = nullptr;

    std::vector<Placement> placements;
    std::set<std::string> platforms; // 去重的平台 ID
    std::set<std::string> groups;    // 去重的组 ID

    int64_t used_volume = 0;
    double total_weight = 0.0;

    // 路线跟踪：platform -> 最远 X 到达点
    // "更深" = 更大的 X。先装载的平台拥有最大的 X。
    std::map<std::string, int32_t> platform_x_max;
    std::map<std::string, int32_t> platform_x_min;

    // 该容器的候选极点列表
    std::vector<Position> extreme_points;

    int32_t inner_x() const noexcept
    {
        return type ? type->inner_size.x : 0;
    }
    int32_t inner_y() const noexcept
    {
        return type ? type->inner_size.y : 0;
    }
    int32_t inner_z() const noexcept
    {
        return type ? type->inner_size.z : 0;
    }

    int64_t total_volume() const noexcept
    {
        return type ? type->inner_size.volume() : 0;
    }

    double volume_rate() const noexcept
    {
        auto tv = total_volume();
        return tv > 0 ? static_cast<double>(used_volume) / static_cast<double>(tv) : 0.0;
    }
};

// 目标向量（字典序，非加权和）
struct ObjectiveVector
{
    int container_count = 0;      // v1: 越小越好
    int total_platforms = 0;      // v2: 越小越好
    double avg_volume_rate = 0.0; // v3: 越大越好
    int group_split_sum = 0;      // v4: 越小越好

    /// 字典序比较：当 *this 严格优于 rhs 时返回 true
    bool is_better_than(const ObjectiveVector& rhs) const noexcept;

    /// 带目标键的版本：只比较 keys 指定的维度
    bool is_better_than(const ObjectiveVector& rhs,
                        const std::vector<std::string>& keys) const noexcept;

    bool operator==(const ObjectiveVector& rhs) const noexcept;
    bool operator!=(const ObjectiveVector& rhs) const noexcept
    {
        return !(*this == rhs);
    }
};

// 输出 / 终止状态类型
enum class Status
{
    Success,
    Timeout,
    FailedConstraint,
    InvalidInput,
};

struct Violation
{
    std::string kind;
    std::vector<std::string> subject_ids;
    std::string details; // 机器可读的短字符串
};

struct ContainerSummary
{
    std::string id;
    std::string type_id;
    Size inner_size; // 容器内部尺寸，绘图所需
    int64_t used_volume = 0;
    int64_t total_volume = 0;
    double volume_rate = 0.0;
    double total_weight = 0.0;
    std::vector<std::string> platforms;
    std::vector<std::string> groups;
};

struct Solution
{
    Status status = Status::Success;
    std::string reason;

    double elapsed_second = 0.0;
    int packed_box_count = 0;
    int unpacked_box_count = 0;
    int container_count = 0;

    std::optional<ObjectiveVector> objective;

    std::vector<ContainerSummary> container_summaries;
    std::vector<std::vector<Placement>> container_placements;
    std::vector<std::string> unpacked_boxes;

    /// 输出 JSON 自包含所需的箱子类型定义
    std::vector<BoxType> box_types;

    /// 实际使用的目标键顺序（输入决定或默认）
    std::vector<std::string> objective_keys;

    std::vector<Violation> violations;
};

// 原因常量
namespace reason
{

inline constexpr const char* k_optimal = "optimal";
inline constexpr const char* k_feasible = "feasible";
inline constexpr const char* k_no_solution = "no_solution";
inline constexpr const char* k_tender_limit = "tender_limit";
inline constexpr const char* k_platform_limit = "platform_limit";
inline constexpr const char* k_support_rate = "support_rate";
inline constexpr const char* k_final_check = "final_check";
inline constexpr const char* k_invalid_range = "invalid_range";
inline constexpr const char* k_duplicate_id = "duplicate_id";
inline constexpr const char* k_route_missing_platform = "route_missing_platform";
} // namespace reason

} // namespace hypercube
