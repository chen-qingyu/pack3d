#pragma once

#include <algorithm>
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

// 斜面（斜切角）：恰好两个带符号轴名截距非零，缺失轴 = 斜面平行贯穿轴；
// 截距 + 从该轴 min 侧向内进深、- 从 max 侧向内；0 = 未设置
struct Facet
{
    int32_t dx = 0;
    int32_t dy = 0;
    int32_t dz = 0;
};

// 斜面归一化：u/v = 两个非零截距轴（0=x,1=y,2=z），w = 平行轴（截距 0）
struct NormalizedFacet
{
    int u_axis = -1;
    int v_axis = -1;
    int w_axis = -1;
    int64_t su = 0; // u 轴带符号截距
    int64_t sv = 0; // v 轴带符号截距
};

[[nodiscard]] inline NormalizedFacet normalize_facet(const Facet& f) noexcept
{
    NormalizedFacet n;
    const int32_t intercepts[3] = {f.dx, f.dy, f.dz};
    for (int a = 0; a < 3; ++a)
    {
        if (intercepts[a] == 0)
        {
            n.w_axis = a;
            continue;
        }
        if (n.u_axis < 0)
        {
            n.u_axis = a;
            n.su = intercepts[a];
        }
        else
        {
            n.v_axis = a;
            n.sv = intercepts[a];
        }
    }
    return n;
}

struct ContainerType
{
    std::string id;
    Size inner_size;
    std::optional<double> payload = std::nullopt; // 装载承重上限（货物总重）
    std::optional<int> quantity_limit;            // null 表示无限制
    std::vector<Obstacle> obstacles;              // 固定占位实体，箱子不得相交，顶面可承托
    std::vector<Facet> facets;                    // 斜切平面禁区，箱子不得侵入，不参与支撑

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
    std::optional<double> weight;        // 箱型级重量（与 boxes 重量互斥，输入重量三选一）
    bool loose = false;                  // 该箱型是否为散件（true=先装托后装车，false=普通箱子直接装车）
    std::optional<std::string> group;    // 箱型级组 ID（与 boxes.group 三选一）
    std::optional<std::string> platform; // 箱型级站点 ID（与 boxes.platform 三选一）
    std::optional<int> quantity;         // 数量展开模式：有值则顶层 boxes 省略，按此数量生成箱子实例

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
    std::string platform;         // 空字符串表示未设置
    std::set<std::string> groups; // 所属分组集合（空=未设置；混组托盘=完整分组集合）
};

// 托盘类型（用户自定义，可多种并存，由装托循环选择）
struct PalletType
{
    std::string id;
    Size size;                       // sx, sy, sz（sz = 托盘自身高度）
    double payload = 0.0;            // 装载承重上限（货物总重，不含托盘自重）
    int max_height = 0;              // 装载限高（货物堆高上限，不含托盘自身高度）
    double self_weight = 0.0;        // 托盘自重
    std::set<std::string> platforms; // 可用平台集合；空 = 全平台可用（含无平台货物）
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
    std::optional<OrientedSize> size = std::nullopt;
    std::set<std::string> groups; // 所属分组集合
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
    bool heavy_not_on_light = false; // 重不压轻：上箱重量 <= 直接支撑箱重量
    std::optional<int> platform_limit;
    std::optional<int> tender_limit;
    std::optional<RouteOrder> route;
    // 承重约束启用标志（presence-based，解析时计算）
    bool has_max_stack = false; // 任一箱型声明了 max_stack
    bool has_max_load = false;  // 任一箱型声明了 max_load

    // 装托（palletizing）：pallet_types 非空即启用装托模式
    std::vector<PalletType> pallet_types;
    bool pallet_fallback = false;         // 散件装不进任何托盘时是否降级散装
    double pallet_support_rate = 1.0;     // 装托阶段专用底面支撑率（默认完全支撑）
    std::optional<bool> pallet_mix_group; // 装托是否允许同一站点混合分组

    // 算法
    Algorithm algorithm = Algorithm::GEP;

    // 数量展开模式标志（解析时：有 quantity 无 boxes 字段时为 true）
    bool quantity_mode = false;

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
    std::optional<double> weight = std::nullopt;
    bool is_pallet = false; // 是否为装托托盘单元（容器内虚拟箱，box_id == pallet_id）

    std::set<std::string> groups; // 所属分组集合（空=未设置；混组托盘=完整分组集合）

    // 堆叠状态（内部字段，不序列化到输出）
    int stack_level = 1;          // 所在堆柱层号，地板层=1（信息性层号）
    int same_run = 1;             // 同箱型连续堆叠层数（含自身）；max_stack 同型连续堆叠用
    double cum_load = 0.0;        // 从上方流经本箱的整柱累计载荷（max_load 累计承重）
    bool has_cross_above = false; // 本箱上方（直接/传递）是否存在异型箱；false=纯同型柱（max_stack 快速路径）
    std::vector<size_t> supports; // 直接支撑箱下标（本容器 placements 内，堆叠传播用）
};

/// 取集合中的唯一分组；集合为空或含多个分组时返回空串
inline std::string single_group(const std::set<std::string>& groups) noexcept
{
    return groups.size() == 1 ? *groups.begin() : std::string();
}

inline std::string encode_groups(const std::set<std::string>& groups)
{
    std::string encoded;
    for (const auto& value : groups)
    {
        encoded += std::to_string(value.size());
        encoded += ':';
        encoded += value;
    }
    return encoded;
}

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

    // 可用容积 = 物理总容积 − 障碍物体积 − 斜面楔形体积（体积率分母）
    int64_t usable_volume() const noexcept
    {
        int64_t usable = total_volume();
        for (const auto& o : type->obstacles)
        {
            usable -= static_cast<int64_t>(o.dx) * o.dy * o.dz;
        }
        const int32_t extents[3] = {type->inner_size.x, type->inner_size.y, type->inner_size.z};
        for (const auto& f : type->facets)
        {
            const auto nf = normalize_facet(f); // 预校验保证恰好两个非零截距
            int64_t mu = (nf.su < 0) ? -nf.su : nf.su;
            int64_t mv = (nf.sv < 0) ? -nf.sv : nf.sv;
            usable -= (mu * mv / 2) * extents[nf.w_axis];
        }
        return usable > 0 ? usable : 1;
    }

    double volume_rate() const noexcept
    {
        return static_cast<double>(used_volume) / static_cast<double>(usable_volume());
    }

    // 实际使用的 X 方向最大长度 = max(placement.x + dx)，无放置为 0
    int32_t used_x() const noexcept
    {
        int32_t ux = 0;
        for (const auto& pl : placements)
        {
            ux = std::max(ux, pl.position.x + pl.osize.dx);
        }
        return ux;
    }

    // X 方向口径可用容积：slab [0, used_x]×[0,sy]×[0,sz] − 障碍物 − 斜面楔形（X 方向体积率分母）
    int64_t usable_volume_x() const noexcept
    {
        const int32_t ux = used_x();
        int64_t usable = static_cast<int64_t>(ux) * inner_y() * inner_z();
        for (const auto& o : type->obstacles)
        {
            // 障碍物完全在容器内，只截断 X 方向与 slab 的重叠
            const int32_t o_hi = o.x + o.dx;
            const int32_t overlap_x = (o.x < ux) ? std::min(o_hi, ux) - o.x : 0;
            if (overlap_x > 0)
            {
                usable -= static_cast<int64_t>(overlap_x) * o.dy * o.dz;
            }
        }
        const int32_t extents[3] = {inner_x(), inner_y(), inner_z()};
        for (const auto& f : type->facets)
        {
            const auto nf = normalize_facet(f); // 预校验保证恰好两个非零截距
            const int64_t m0 = (nf.su < 0) ? -nf.su : nf.su;
            const int64_t m1 = (nf.sv < 0) ? -nf.sv : nf.sv;
            if (nf.w_axis == 0)
            {
                // 贯穿轴为 X：楔形沿 X 全长，slab 内体积 = 截面 × used_x
                usable -= (m0 * m1 / 2) * ux;
                continue;
            }
            // 贯穿轴非 X：u/v 恰含 X 轴，楔形 X 向截面高度随 x 线性变化（梯形体积）
            const int x_idx = (nf.u_axis == 0) ? 0 : 1;
            const int64_t mx = (x_idx == 0) ? m0 : m1; // X 轴截距
            const int64_t sx = (x_idx == 0) ? nf.su : nf.sv;
            const int64_t mq = (x_idx == 0) ? m1 : m0; // 另一截距轴
            const int64_t wedge_x0 = (sx < 0) ? static_cast<int64_t>(extents[0]) - mx : 0;
            int64_t x0 = std::max<int64_t>(wedge_x0, 0);
            int64_t x1 = std::min<int64_t>((sx < 0) ? static_cast<int64_t>(extents[0]) : mx, ux);
            if (x1 > x0)
            {
                // 截面高度 dv(x) = mq·(x − wedge_x0)/mx（sx<0）或 mq·(mx − x)/mx（sx>0）
                const double dv0 = static_cast<double>(mq) * (sx < 0 ? x0 - wedge_x0 : mx - x0) / mx;
                const double dv1 = static_cast<double>(mq) * (sx < 0 ? x1 - wedge_x0 : mx - x1) / mx;
                usable -= static_cast<int64_t>((dv0 + dv1) * 0.5 * extents[nf.w_axis] * (x1 - x0));
            }
        }
        return usable > 0 ? usable : 1;
    }

    double volume_rate_x() const noexcept
    {
        return static_cast<double>(used_volume) / static_cast<double>(usable_volume_x());
    }
};

// 装托结果：一个托盘 + 其上箱子（容器 placements 中虚拟箱 box_id == pallet_id）
struct PalletLoad
{
    std::string pallet_id; // 形如 "pt1200#1"
    std::string type_id;   // PalletType.id
    const PalletType* type = nullptr;
    std::vector<Placement> placements; // 托盘上的箱子（复用 Placement）
    int loaded_height = 0;             // 货物顶高（不含托盘 sz）
    double goods_weight = 0.0;         // 货物总重（不含自重）
    std::set<std::string> groups;
    std::set<std::string> platforms;
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
    std::vector<Facet> facets;       // 本容器实例的斜面（类型继承）
    std::optional<double> payload = std::nullopt;
    int64_t used_volume = 0;
    double volume_rate = 0.0;
    double volume_rate_x = 0.0; // X 方向口径：装箱体积 / (实际使用 X 最大长度 × 容器宽高)
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

    // 装托字段（未启用装托时全为 0 / 空数组，恒输出）
    std::vector<PalletLoad> pallets;
    int pallet_count = 0;         // 托盘单元数
    int palletized_box_count = 0; // 已装托的散件箱数
    int loose_box_count = 0;      // 直接装车的箱子数（普通箱子 + fallback 降级散件）

    std::vector<std::string> violations;
};

} // namespace pack3d
