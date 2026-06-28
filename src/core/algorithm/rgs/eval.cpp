#include "eval.hpp"

#include <algorithm>

namespace pack3d::rgs
{

int count_fit_uld_types(
    const Box& box,
    const BoxType& box_type,
    const std::vector<ContainerType>& container_types) noexcept
{
    int count = 0;
    for (const auto& ct : container_types)
    {
        for (auto o : box_type.allowed_orientations)
        {
            auto os = box_type.size.orient(o);
            if (os.dx <= ct.inner_size.x &&
                os.dy <= ct.inner_size.y &&
                os.dz <= ct.inner_size.z)
            {
                if (ct.max_weight.has_value() && box.weight.has_value())
                {
                    if (box.weight.value() > ct.max_weight.value() + 1e-9)
                    {
                        continue;
                    }
                }
                ++count;
                break;
            }
        }
    }
    return count > 0 ? count : 1;
}

double compute_penalty_denom(
    const std::vector<Box>& all_boxes,
    const std::map<std::string, BoxType>& box_type_map,
    const std::vector<ContainerType>& container_types) noexcept
{
    int total_uld_types = static_cast<int>(container_types.size());
    if (total_uld_types <= 1)
    {
        return 1.0;
    }

    double denom = 0.0;
    for (const auto& bx : all_boxes)
    {
        auto bt_it = box_type_map.find(bx.box_type_id);
        if (bt_it == box_type_map.end())
        {
            continue;
        }
        int u_i = count_fit_uld_types(bx, bt_it->second, container_types);
        double v_i = static_cast<double>(bt_it->second.size.volume());
        denom += static_cast<double>(total_uld_types - u_i) * v_i;
    }
    return denom > 0.0 ? denom : 1.0;
}

double score_uld(
    const ContainerLoad& load,
    const std::vector<std::string>& unloaded_box_ids,
    const std::map<std::string, Box>& box_map,
    const std::map<std::string, BoxType>& box_type_map,
    const std::vector<ContainerType>& container_types,
    double total_penalty_denom) noexcept
{
    double sv = load.volume_rate();

    int total_uld_types = static_cast<int>(container_types.size());
    if (total_uld_types <= 1)
    {
        return sv;
    }

    double penalty_num = 0.0;
    for (const auto& bid : unloaded_box_ids)
    {
        auto bx_it = box_map.find(bid);
        if (bx_it == box_map.end())
        {
            continue;
        }
        const auto& bx = bx_it->second;
        auto bt_it = box_type_map.find(bx.box_type_id);
        if (bt_it == box_type_map.end())
        {
            continue;
        }
        int u_i = count_fit_uld_types(bx, bt_it->second, container_types);
        double v_i = static_cast<double>(bt_it->second.size.volume());
        penalty_num += static_cast<double>(total_uld_types - u_i) * v_i;
    }

    double penalty = penalty_num / total_penalty_denom;
    return sv - penalty;
}

} // namespace pack3d::rgs
