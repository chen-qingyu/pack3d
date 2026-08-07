#pragma once

#include <cassert>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "algorithm/config.hpp"

namespace pack3d
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

struct Size
{
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    int64_t volume() const noexcept
    {
        return static_cast<int64_t>(x) * y * z;
    }

    OrientedSize orient(Orientation o) const noexcept
    {
        switch (o)
        {
            case Orientation::XYZ:
                return {x, y, z};
            case Orientation::XZY:
                return {x, z, y};
            case Orientation::YXZ:
                return {y, x, z};
            case Orientation::YZX:
                return {y, z, x};
            case Orientation::ZXY:
                return {z, x, y};
            case Orientation::ZYX:
                return {z, y, x};
            default:
                assert(false && "Invalid orientation");
                return {x, y, z};
        }
    }
};

struct Position
{
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    auto operator<=>(const Position&) const = default;
};

// 容器内障碍物（轴对齐长方体，实体，顶面等价地板可承托箱子）
struct Obstacle
{
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
    int32_t dx = 0;
    int32_t dy = 0;
    int32_t dz = 0;
};

struct ContainerType
{
    std::string id;
    Size inner_size;
    std::optional<double> max_weight = std::nullopt;
    std::optional<int> quantity_limit; // null 表示无限制
    std::vector<Obstacle> obstacles;   // 固定占位实体，箱子不得相交，顶面可承托

    bool has_remaining(const std::map<std::string, int>& usage) const noexcept
    {
        auto it = usage.find(id);
        int used = (it != usage.end()) ? it->second : 0;
        return !quantity_limit.has_value() || used < quantity_limit.value();
    }
};

struct BoxType
{
    std::string id;
    Size size;
    std::vector<Orientation> allowed_orientations;
    // 与 allowed_orientations 对齐：每朝向的堆码层数 / 单箱上方承重上限；nullopt=该朝向不限
    std::vector<std::optional<int>> max_stack;
    std::vector<std::optional<double>> max_load;

    /// 按朝向查堆码层数上限（无此朝向或未配置则返回 nullopt）
    [[nodiscard]] std::optional<int> max_stack_for(Orientation o) const noexcept
    {
        for (size_t i = 0; i < allowed_orientations.size() && i < max_stack.size(); ++i)
        {
            if (allowed_orientations[i] == o)
            {
                return max_stack[i];
            }
        }
        return std::nullopt;
    }

    /// 按朝向查单箱上方承重上限
    [[nodiscard]] std::optional<double> max_load_for(Orientation o) const noexcept
    {
        for (size_t i = 0; i < allowed_orientations.size() && i < max_load.size(); ++i)
        {
            if (allowed_orientations[i] == o)
            {
                return max_load[i];
            }
        }
        return std::nullopt;
    }
};

struct Box
{
    std::string id;
    std::string box_type_id;
    std::optional<double> weight = std::nullopt;
    std::string group;    // 空字符串表示未设置
    std::string platform; // 空字符串表示未设置
};

// 路线
struct RouteOrder
{
    std::vector<std::string> platform_order;
    std::map<std::string, size_t> index_of; // platform -> 在顺序中的位置
};

// 求解算法
enum class Algorithm : uint8_t
{
    GEP, // 极点贪心（默认）
    GLC, // 贪心前瞻构造
    RGS, // 随机贪心搜索
    BSG, // 束搜索集装箱装载 (BSG-CLP)
};

// 求解状态
enum class SolveStatus : uint8_t
{
    Complete,
    Invalid,
    Timeout,
    Partial,
};

// 已有容器中的放置（自包含，无需查表）
struct ExistingPlacement
{
    std::string box_id;
    std::string box_type_id;
    Position position;
    Orientation orientation = Orientation::XYZ;
    std::optional<double> weight = std::nullopt;
    std::string platform;
    std::string group;
    std::optional<OrientedSize> size = std::nullopt;
};

struct ExistingContainer
{
    std::string type_id;
    std::vector<ExistingPlacement> placements;
};

// 完整问题描述
struct Problem
{
    std::vector<ContainerType> container_types;
    std::vector<BoxType> box_types;
    std::vector<Box> boxes;

    // 约束（默认值唯一来源；time_limit 见 config.hpp）
    double time_limit = config::TIME_LIMIT;
    double support_rate = 0.0;
    std::optional<int> platform_limit;
    std::optional<int> tender_limit;
    std::optional<RouteOrder> route;
    // 承重约束启用标志（presence-based，解析时计算）
    bool has_max_stack = false; // 任一箱型声明了 max_stack
    bool has_max_load = false;  // 任一箱型声明了 max_load

    // 算法
    Algorithm algorithm = Algorithm::GEP;

    // 已有的中间状态（每个容器已放置的箱子）
    std::vector<ExistingContainer> existing_containers;
};

// 放置结果（内部 + 输出）
struct Placement
{
    std::string box_id;
    std::string box_type_id;
    std::string container_id;
    Position position;
    Orientation orientation = Orientation::XYZ;
    OrientedSize osize;   // 朝向后的实际尺寸
    std::string platform; // 空字符串表示未设置，输出时转为 null
    std::string group;    // 空字符串表示未设置，输出时转为 null
    std::optional<double> weight = std::nullopt;

    // 堆叠状态（内部字段，不序列化到输出）
    double supported_load = 0.0; // 其上直接承重累计（仅 max_load 约束使用）
    int stack_level = 1;         // 所在堆柱层号，地板层=1（仅 max_stack 约束使用）
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

    bool locked = false; // 已有容器，后处理不可移动

    int32_t inner_x() const noexcept
    {
        return type->inner_size.x;
    }
    int32_t inner_y() const noexcept
    {
        return type->inner_size.y;
    }
    int32_t inner_z() const noexcept
    {
        return type->inner_size.z;
    }

    int64_t total_volume() const noexcept
    {
        return type->inner_size.volume();
    }

    double volume_rate() const noexcept
    {
        return static_cast<double>(used_volume) / static_cast<double>(total_volume());
    }
};

// 目标向量（字典序，非加权和）
struct ObjectiveVector
{
    int container_count = 0;      // v1: 越小越好
    int platform_split = 0;       // v2: 越小越好
    double avg_volume_rate = 0.0; // v3: 越大越好
    int group_split_sum = 0;      // v4: 越小越好

    /// 字典序比较：当 *this 严格优于 rhs 时返回 true
    bool is_better_than(const ObjectiveVector& rhs) const noexcept;

    bool operator==(const ObjectiveVector& rhs) const noexcept;
    bool operator!=(const ObjectiveVector& rhs) const noexcept
    {
        return !(*this == rhs);
    }
};

struct ContainerSummary
{
    std::string type_id;
    Size inner_size;                 // 容器内部尺寸，绘图所需
    std::vector<Obstacle> obstacles; // 本容器实例的障碍物（类型继承）
    std::optional<double> max_weight = std::nullopt;
    int64_t used_volume = 0;
    double volume_rate = 0.0;
    std::optional<double> used_weight = std::nullopt;
    std::optional<double> weight_rate = std::nullopt;
    int packed_count = 0; // 本容器内放置的箱子数
    std::vector<std::string> platforms;
    std::vector<std::string> groups;
    std::optional<int> tender = std::nullopt; // 所属 tender 序号（1-based），无 group 为 null
};

struct Solution
{
    SolveStatus status = SolveStatus::Complete;

    double elapsed_second = 0.0;
    int packed_box_count = 0;
    int unpacked_box_count = 0;

    ObjectiveVector objective;

    std::vector<ContainerSummary> container_summaries;
    std::vector<std::vector<Placement>> container_placements;
    std::vector<std::string> unpacked_boxes;

    /// 输出 JSON 自包含所需的箱子类型定义
    std::vector<BoxType> box_types;

    std::vector<std::string> violations;
};

} // namespace pack3d
