#include "packer.hpp"

#include <algorithm>
#include <set>

#include "../../tool.hpp"
#include "../config.hpp"
#include "block.hpp"
#include "solver.hpp"
#include "types.hpp"

namespace pack3d
{

ContainerLoad BsgPacker::pack_single(
    const std::vector<Box>& items,
    const ContainerType& ct)
{
    // 构建 BoxType 索引
    std::vector<BoxType> box_types;
    std::map<std::string, int> type_idx_map;
    for (const auto& bt : problem_.box_types)
    {
        type_idx_map[bt.id] = static_cast<int>(box_types.size());
        box_types.push_back(bt);
    }
    int n_types = static_cast<int>(box_types.size());

    // 统计 per-type 数量和 ID 列表
    std::vector<int> cur_counts(n_types, 0);
    std::vector<std::vector<std::string>> cur_ids_by_type(n_types);
    for (const auto& bx : items)
    {
        auto it = type_idx_map.find(bx.box_type_id);
        if (it == type_idx_map.end())
        {
            continue;
        }
        int ti = it->second;
        cur_counts[ti]++;
        cur_ids_by_type[ti].push_back(bx.id);
    }

    // 块生成
    double max_fr = (n_types < config::BSG_THRESHOLD_BOX_TYPES)
                        ? config::BSG_MAX_FR_WEAK
                        : config::BSG_MAX_FR_STRONG;
    auto blocks = bsg::generate_blocks(ct.inner_size, box_types, cur_counts,
                                       max_fr, config::BSG_MAX_BL);

    bsg::GlobalContext ctx;
    ctx.container_size = ct.inner_size;
    ctx.box_types = std::move(box_types);
    ctx.blocks = std::move(blocks);

    // 求解
    bsg::PackResult pr = bsg::solve(ctx, cur_counts, cur_ids_by_type, problem_.time_limit);

    ContainerLoad load;
    load.type_id = ct.id;
    load.type = &ct;
    load.placements = std::move(pr.placements);
    load.used_volume = pr.used_volume;

    // 补充追踪字段（BSG 不解平台/分组，从 box_map 回填）
    for (auto& pl : load.placements)
    {
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
            int32_t xmax = pl.position.x + pl.osize.dx;
            auto it = load.platform_x_max.find(bx.platform);
            if (it == load.platform_x_max.end() || xmax > it->second)
            {
                load.platform_x_max[bx.platform] = xmax;
            }
            auto it2 = load.platform_x_min.find(bx.platform);
            if (it2 == load.platform_x_min.end() || pl.position.x < it2->second)
            {
                load.platform_x_min[bx.platform] = pl.position.x;
            }
        }
        if (!bx.group.empty())
        {
            load.groups.insert(bx.group);
        }
        if (has_weight_info_ && bx.weight.has_value())
        {
            load.total_weight += bx.weight.value();
        }
    }

    return load;
}

} // namespace pack3d
