#include "select_container.hpp"

#include <algorithm>

namespace pack3d
{

const ContainerType* select_largest_fitting(
    const std::vector<ContainerType>& container_types,
    const std::map<std::string, int>& usage,
    const std::vector<Box>& remaining,
    const std::map<std::string, BoxType>& box_type_map) noexcept
{
    std::vector<size_t> indices;
    indices.reserve(container_types.size());
    for (size_t i = 0; i < container_types.size(); ++i)
    {
        indices.push_back(i);
    }
    std::sort(indices.begin(), indices.end(),
              [&](size_t a, size_t b)
              {
                  return container_types[a].inner_size.volume() >
                         container_types[b].inner_size.volume();
              });

    for (size_t idx : indices)
    {
        const auto& ct = container_types[idx];
        if (!ct.has_remaining(usage))
        {
            continue;
        }

        for (const auto& bx : remaining)
        {
            auto bt_it = box_type_map.find(bx.box_type_id);
            if (bt_it == box_type_map.end())
            {
                continue;
            }
            for (auto o : bt_it->second.allowed_orientations)
            {
                auto os = bt_it->second.size.orient(o);
                if (os.dx <= ct.inner_size.x && os.dy <= ct.inner_size.y && os.dz <= ct.inner_size.z)
                {
                    if (ct.max_weight.has_value() && bx.weight.has_value())
                    {
                        if (bx.weight.value() <= ct.max_weight.value() + 1e-9)
                        {
                            return &ct;
                        }
                    }
                    else
                    {
                        return &ct;
                    }
                }
            }
        }
    }

    return nullptr;
}

} // namespace pack3d
