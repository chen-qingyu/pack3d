#include "packer.hpp"

#include <algorithm>

#include "../../constraints.hpp"

namespace pack3d
{

ContainerLoad GepPacker::pack_single(
    const std::vector<Box>& items,
    const ContainerType& ct)
{
    // 箱子按体积降序排序
    std::vector<Box> sorted = items;
    std::sort(sorted.begin(), sorted.end(),
              [&](const Box& a, const Box& b)
              {
                  int64_t va = box_type_map_.at(a.box_type_id).size.volume();
                  int64_t vb = box_type_map_.at(b.box_type_id).size.volume();
                  return va > vb;
              });

    ContainerLoad load;
    load.type_id = ct.id;
    load.type = &ct;

    // 候选点列表（极点的 x,y,z，去重用 set）
    struct Ep
    {
        int32_t x, y, z;
        bool operator<(const Ep& o) const
        {
            if (z != o.z)
                return z < o.z;
            if (y != o.y)
                return y < o.y;
            return x < o.x;
        }
    };
    std::set<Ep> eps;
    eps.insert({0, 0, 0});

    for (auto& box : sorted)
    {
        const auto& bt = box_type_map_.at(box.box_type_id);
        bool placed = false;

        for (auto orient : bt.allowed_orientations)
        {
            OrientedSize os = bt.size.orient(orient);

            for (const auto& ep : eps)
            {
                Position pos{ep.x, ep.y, ep.z};

                // 边界检查
                if (!check_boundary(ct, pos, os))
                {
                    continue;
                }
                if (check_overlap(pos, os, load.placements))
                {
                    continue;
                }

                // 重量检查
                if (has_weight_info_ && !check_weight(load, box.weight.value()))
                {
                    continue;
                }

                // 支撑率检查
                if (!check_support(pos, os, load, box_type_map_, problem_.support_rate))
                {
                    continue;
                }

                // 平台数量限制
                if (problem_.platform_limit.has_value() && !box.platform.empty())
                {
                    if (!check_platform_limit(load, box.platform,
                                              problem_.platform_limit.value()))
                    {
                        continue;
                    }
                }

                // 路线顺序
                if (problem_.route.has_value() && !box.platform.empty())
                {
                    if (!check_route_order(load, box.platform, pos, os,
                                           problem_.route.value()))
                    {
                        continue;
                    }
                }

                // 放置
                Placement pl;
                pl.box_id = box.id;
                pl.box_type_id = box.box_type_id;
                pl.position = pos;
                pl.orientation = orient;
                pl.osize = os;
                pl.platform = box.platform;
                pl.group = box.group;

                load.placements.push_back(pl);
                load.used_volume += os.volume();
                if (has_weight_info_)
                {
                    load.total_weight += box.weight.value();
                }
                if (!box.platform.empty())
                {
                    load.platforms.insert(box.platform);
                    int32_t xmax = pos.x + os.dx;
                    auto it = load.platform_x_max.find(box.platform);
                    if (it == load.platform_x_max.end() || xmax > it->second)
                    {
                        load.platform_x_max[box.platform] = xmax;
                    }
                    auto it2 = load.platform_x_min.find(box.platform);
                    if (it2 == load.platform_x_min.end() || pos.x < it2->second)
                    {
                        load.platform_x_min[box.platform] = pos.x;
                    }
                }
                if (!box.group.empty())
                {
                    load.groups.insert(box.group);
                }

                // 生成新极点
                eps.insert({pos.x + os.dx, pos.y, pos.z});
                eps.insert({pos.x, pos.y + os.dy, pos.z});
                eps.insert({pos.x, pos.y, pos.z + os.dz});

                placed = true;
                break;
            }
            if (placed)
                break;
        }
        // 放不下就跳过此箱子
    }

    return load;
}

} // namespace pack3d
