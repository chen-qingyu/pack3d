#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../constraints.hpp"
#include "../../types.hpp"

namespace pack3d::bsg
{

// ============================================================
// Cuboid — 残差空间 cover representation (K1)
// ============================================================
struct Cuboid
{
    Position pos;   // anchor corner (x_min, y_min, z_min)
    int32_t lx = 0; // length along X
    int32_t ly = 0; // length along Y
    int32_t lz = 0; // length along Z

    int32_t x_max() const noexcept
    {
        return pos.x + lx;
    }
    int32_t y_max() const noexcept
    {
        return pos.y + ly;
    }
    int32_t z_max() const noexcept
    {
        return pos.z + lz;
    }

    int64_t volume() const noexcept
    {
        return static_cast<int64_t>(lx) * ly * lz;
    }

    bool contains(const Cuboid& other) const noexcept
    {
        return pos.x <= other.pos.x && x_max() >= other.x_max() &&
               pos.y <= other.pos.y && y_max() >= other.y_max() &&
               pos.z <= other.pos.z && z_max() >= other.z_max();
    }

    bool overlaps(const Cuboid& other) const noexcept
    {
        return pos.x < other.x_max() && x_max() > other.pos.x &&
               pos.y < other.y_max() && y_max() > other.pos.y &&
               pos.z < other.z_max() && z_max() > other.pos.z;
    }
};

// ============================================================
// GeneralBlock — 统一块结构 (K2)
// ============================================================
struct GeneralBlock
{
    struct Member
    {
        int type_idx = 0; // index into box_types array
        int count = 0;
    };

    enum class MergeAxis : uint8_t
    {
        None, // simple block (no merge)
        X,    // merged along X axis
        Y,
        Z,
    };

    int64_t id = 0;                // unique block id
    OrientedSize osize;            // outer bounding dimensions
    std::vector<Member> members;   // box types + counts composing this block
    int total_box_count = 0;       // sum of member counts
    int64_t single_box_volume = 0; // sum of individual box volumes (for fill rate)

    // Merge tree (for final placement reconstruction)
    // 使用 block id 而非数组索引（因为 blocks 数组会被排序）
    MergeAxis merge_axis = MergeAxis::None;
    int64_t source_left_id = -1;  // global block id of left/bottom source
    int64_t source_right_id = -1; // global block id of right/top source

    // Simple block layout (valid when merge_axis == None)
    int nx = 0, ny = 0, nz = 0;                 // grid repeat counts
    Orientation orientation = Orientation::XYZ; // base box orientation
    int type_idx = -1;                          // box type index (simple blocks only)

    int64_t volume() const noexcept
    {
        return osize.volume();
    }

    double fill_rate() const noexcept
    {
        if (volume() <= 0)
        {
            return 0.0;
        }
        return static_cast<double>(single_box_volume) / static_cast<double>(volume());
    }

    bool is_simple() const noexcept
    {
        return members.size() == 1;
    }
};

// ============================================================
// BSGState — beam search 状态
// ============================================================
struct BSGState
{
    // 残差 cuboid 集合 (K1 cover representation)
    std::vector<Cuboid> R;

    // 剩余箱子：per box_type 剩余件数（索引对应全局 box_types 数组）
    std::vector<int> remaining_counts;

    // 可用块索引（全局 blocks 数组的子集）
    std::vector<int> available_blocks;

    // 已放置的块记录
    struct PlacedBlock
    {
        int block_idx = 0; // 全局 blocks 索引
        Position anchor;   // 放置的 anchor corner
    };
    std::vector<PlacedBlock> placements;

    // 已用体积
    int64_t used_volume = 0;

    // 项目约束使用的叶子箱状态；仅在 item_classes 非空时维护。
    ContainerLoad constraint_load;
    std::vector<int> item_class_indices;

    // KPA 缓存 (K4) — lazy init
    // kpa_X[c] = max linear extension along axis X using remaining boxes with capacity c
    std::optional<std::vector<int>> kpa_L;
    std::optional<std::vector<int>> kpa_W;
    std::optional<std::vector<int>> kpa_H;
};

struct ItemClass
{
    std::string box_type_id;
    std::string platform;
    double weight = 0.0;
    std::vector<std::string> box_ids;
    std::set<std::string> groups; // 所属分组集合
};

// ============================================================
// GlobalContext — 全局不可变数据（所有 state 共享）
// ============================================================
struct GlobalContext
{
    // 容器尺寸
    Size container_size;
    ContainerType container_type;

    // 最小底面支撑率；0 表示不启用支撑约束
    double support_rate = 0.0;

    // 承重约束启用标志
    bool has_max_stack = false;
    bool has_max_load = false;
    bool heavy_not_on_light = false; // 重不压轻：上箱重量 <= 直接支撑箱重量

    // 箱子类型（索引即 type_idx）
    std::vector<BoxType> box_types;
    std::vector<ItemClass> item_classes;
    std::map<std::string, BoxType> box_type_map;
    std::optional<int> platform_limit;
    std::optional<RouteOrder> route;
    bool has_weight_info = false;
    bool has_platform = false; // 任一 item class 含非空 platform（count_remaining_platforms 快速路径）

    // 已提交容器的 tender 分解（本容器装载期间不可变；limit<=0 未启用）
    TenderState tender;

    // 全局块列表（GeneralBlockGeneration 产物）
    std::vector<GeneralBlock> blocks;

    // block id → blocks 数组索引（packer 构建一次，feasibility 复用）
    std::unordered_map<int64_t, int> block_indices;

    /// 是否存在需要逐叶校验的项目约束（重量/平台上限/路线/堆码/承重/tender/facets）。
    /// 支撑由 is_supported 在块级处理；障碍物在 support_rate>0 时强制逐叶（快路径的
    /// is_supported 不认障碍物顶面支撑）；support_rate==0 时雕刻已保证空间无禁区，快路径即可。
    /// 斜面不做阶梯雕刻（阶梯碎片会切碎残余空间、降低装载），facets 存在即强制逐叶，
    /// 由 can_place_block 的 check_facet 逐叶兜底。
    [[nodiscard]] bool needs_leaf_validation() const noexcept
    {
        return !container_type.facets.empty() ||
               (!container_type.obstacles.empty() && support_rate > 0.0) ||
               has_weight_info || platform_limit.has_value() || route.has_value() ||
               has_max_stack || has_max_load || heavy_not_on_light || tender.limit > 0;
    }

    std::vector<Placement> existing_placements;
};

// ============================================================
// PackResult (bsg 内部)
// ============================================================
struct PackResult
{
    bool success = false;
    std::vector<Placement> placements;
    std::vector<std::string> unpacked_box_ids;
    int64_t used_volume = 0;
};

} // namespace pack3d::bsg
