#include "packer.hpp"

#include <algorithm>
#include <set>
#include <tuple>

#include "../../tool.hpp"
#include "../config.hpp"
#include "block.hpp"
#include "solver.hpp"
#include "types.hpp"

namespace pack3d
{

ContainerLoad BsgPacker::pack_single_impl(
    const std::vector<Box>& items,
    const ContainerType& ct,
    const std::vector<Placement>& existing,
    const TenderState& tender,
    bool /*stop_when_complete*/)
{
    // 按影响硬约束的属性划分库存类，避免搜索后再补平台和重量。
    // tender 启用时 group 入键，使束搜索期间能按块判 group；未启用则与旧行为一致。
    const bool use_tender = problem_.tender_limit.has_value();
    std::vector<BoxType> box_types;
    std::vector<bsg::ItemClass> item_classes;
    std::map<std::tuple<std::string, std::string, std::string, double>, int> class_index;
    for (const auto& bx : items)
    {
        auto bt_it = box_type_map_.find(bx.box_type_id);
        if (bt_it == box_type_map_.end())
        {
            continue;
        }
        const double weight = bx.weight.value_or(0.0);
        const std::string gkey = use_tender ? bx.group : "";
        const auto key = std::make_tuple(bx.box_type_id, bx.platform, gkey, weight);
        auto [it, inserted] = class_index.emplace(key, static_cast<int>(item_classes.size()));
        if (inserted)
        {
            box_types.push_back(bt_it->second);
            item_classes.push_back({bx.box_type_id, bx.platform, weight, gkey, {}});
        }
        item_classes[it->second].box_ids.push_back(bx.id);
    }

    std::vector<int> cur_counts;
    std::vector<std::vector<std::string>> cur_ids_by_type;
    cur_counts.reserve(item_classes.size());
    cur_ids_by_type.reserve(item_classes.size());
    for (const auto& item_class : item_classes)
    {
        cur_counts.push_back(static_cast<int>(item_class.box_ids.size()));
        cur_ids_by_type.push_back(item_class.box_ids);
    }
    int n_types = static_cast<int>(box_types.size());

    // 块生成
    double max_fr = (n_types < config::BSG_THRESHOLD_BOX_TYPES)
                        ? config::BSG_MAX_FR_WEAK
                        : config::BSG_MAX_FR_STRONG;
    auto blocks = bsg::generate_blocks(ct.inner_size, box_types, cur_counts,
                                       max_fr, config::BSG_MAX_BL);

    bsg::GlobalContext ctx;
    ctx.container_size = ct.inner_size;
    ctx.container_type = ct;
    ctx.support_rate = problem_.support_rate;
    ctx.has_max_stack = problem_.has_max_stack;
    ctx.has_max_load = problem_.has_max_load;
    ctx.heavy_not_on_light = problem_.heavy_not_on_light;
    ctx.box_types = std::move(box_types);
    ctx.item_classes = std::move(item_classes);
    ctx.has_platform = std::any_of(ctx.item_classes.begin(), ctx.item_classes.end(),
                                   [](const bsg::ItemClass& ic) noexcept
                                   { return !ic.platform.empty(); });
    ctx.box_type_map = box_type_map_;
    ctx.platform_limit = problem_.platform_limit;
    ctx.route = problem_.route;
    ctx.has_weight_info = has_weight_info_;
    ctx.tender = tender;
    ctx.blocks = std::move(blocks);
    ctx.existing_placements = existing;

    // 构建 block id → index 映射（feasibility 复用，避免每次 can_place_block 重建）
    ctx.block_indices.reserve(ctx.blocks.size());
    for (int i = 0; i < static_cast<int>(ctx.blocks.size()); ++i)
    {
        ctx.block_indices.emplace(ctx.blocks[i].id, i);
    }

    // 求解
    bsg::PackResult pr = bsg::solve(ctx, cur_counts, cur_ids_by_type);

    ContainerLoad load;
    load.type_id = ct.id;
    load.type = &ct;

    // 已有放置：先于 solver 结果加入，使 BSG 与其他算法一致（used_volume 随后由 solver 值覆盖）
    prefill_load(load, existing, box_map_);

    // solver 新放置
    load.placements.insert(load.placements.end(),
                           std::make_move_iterator(pr.placements.begin()),
                           std::make_move_iterator(pr.placements.end()));
    // pr.used_volume 含已有体积（solver 内部 s0.used_volume 预填），直接用
    load.used_volume = pr.used_volume;

    // 回填不影响 BSG 搜索的分组元数据（仅新放置需要）
    for (size_t i = existing.size(); i < load.placements.size(); ++i)
    {
        auto& pl = load.placements[i];
        auto bx_it = box_map_.find(pl.box_id);
        if (bx_it == box_map_.end())
        {
            continue;
        }
        const auto& bx = bx_it->second;
        pl.platform = bx.platform;
        pl.group = bx.group;
        if (!bx.platform.empty())
        {
            load.platforms.insert(bx.platform);
        }
        if (!bx.group.empty())
        {
            load.groups.insert(bx.group);
        }
        if (has_weight_info_ && bx.weight.has_value())
        {
            load.total_weight += bx.weight.value();
        }
        pl.weight = bx.weight;
    }

    return load;
}

} // namespace pack3d
