#pragma once

#include <cassert>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

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

struct ContainerType
{
    std::string id;
    Size inner_size;
    std::optional<double> max_weight = std::nullopt;
    std::optional<int> quantity_limit; // null 表示无限制

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
    bool stackable = true;
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
    GEP, // 贪心极点算法（默认）
    GLC, // 贪心前瞻构造
    RGS, // 随机贪心搜索
    BSG, // 束搜索集装箱装载 (BSG-CLP)
};

// 求解状态
enum class SolveStatus : uint8_t
{
    Complete,
    Invalid,
    Blocked,
    Timeout,
    Partial,
};

// 完整问题描述
struct Problem
{
    std::vector<ContainerType> container_types;
    std::vector<BoxType> box_types;
    std::vector<Box> boxes;

    // 约束
    double time_limit = 0.0;
    double support_rate = 0.0;
    std::optional<int> platform_limit;
    std::optional<int> tender_limit;
    std::optional<RouteOrder> route;

    // 算法
    Algorithm algorithm = Algorithm::GEP;
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

    // 路线跟踪：platform -> X 方向边界
    // 更深 = 更小的 X（靠里）。先装的平台在最深处（X 最小）。
    std::map<std::string, int32_t> platform_x_max;
    std::map<std::string, int32_t> platform_x_min;

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
    Size inner_size; // 容器内部尺寸，绘图所需
    std::optional<double> max_weight = std::nullopt;
    int64_t used_volume = 0;
    double volume_rate = 0.0;
    std::optional<double> used_weight = std::nullopt;
    std::optional<double> weight_rate = std::nullopt;
    int packed_count = 0; // 本容器内放置的箱子数
    std::vector<std::string> platforms;
    std::vector<std::string> groups;
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
