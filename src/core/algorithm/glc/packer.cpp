#include "packer.hpp"

#include "../../tool.hpp"
#include "../config.hpp"
#include "block.hpp"

namespace pack3d
{

ContainerLoad GlcPacker::pack_single(
    const std::vector<Box>& items,
    const ContainerType& ct,
    bool /*stop_when_complete*/)
{
    std::vector<const Box*> box_ptrs;
    box_ptrs.reserve(items.size());
    for (const auto& bx : items)
    {
        box_ptrs.push_back(&bx);
    }

    auto pr_opt = pack_container(&ct, box_ptrs);

    ContainerLoad load;
    load.type_id = ct.id;
    load.type = &ct;

    if (pr_opt.has_value())
    {
        auto& pr = pr_opt.value();
        load.placements = std::move(pr.placements);
        load.used_volume = pr.used_volume;
        load.total_weight = pr.total_weight;
        load.platforms = std::move(pr.platforms);
        load.groups = std::move(pr.groups);
        load.platform_x_max = std::move(pr.platform_x_max);
        load.platform_x_min = std::move(pr.platform_x_min);
    }

    return load;
}

std::optional<glc::PackResult> GlcPacker::pack_container(
    const ContainerType* ct,
    const std::vector<const Box*>& boxes) const
{
    std::vector<Box> box_list;
    for (const auto* bp : boxes)
    {
        box_list.push_back(*bp);
    }

    glc::Heuristic heuristic(*ct, box_type_map_, box_map_, problem_, has_weight_info_);
    glc::PackResult pr = heuristic.pack_beam(box_list, config::GLC_WIDTH);
    if (pr.success || !pr.placements.empty())
    {
        return pr;
    }
    return std::nullopt;
}

} // namespace pack3d
