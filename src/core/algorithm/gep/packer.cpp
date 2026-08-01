#include "packer.hpp"

#include <algorithm>

#include "../../constraints.hpp"

namespace pack3d
{

ContainerLoad GepPacker::pack_single(
    const std::vector<Box>& items,
    const ContainerType& ct,
    const std::vector<Placement>& existing,
    bool /*stop_when_complete*/)
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

    // 预填充已有放置
    for (const auto& pl : existing)
    {
        load.placements.push_back(pl);
        load.used_volume += pl.osize.volume();
        if (!pl.platform.empty())
        {
            load.platforms.insert(pl.platform);
        }
        if (!pl.group.empty())
        {
            load.groups.insert(pl.group);
        }
        auto bx_it = box_map_.find(pl.box_id);
        if (bx_it != box_map_.end() && bx_it->second.weight.has_value())
        {
            load.total_weight += bx_it->second.weight.value();
        }
    }

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

    // 从已有放置生成初始 EP
    for (const auto& pl : existing)
    {
        eps.insert({pl.position.x + pl.osize.dx, pl.position.y, pl.position.z});
        eps.insert({pl.position.x, pl.position.y + pl.osize.dy, pl.position.z});
        eps.insert({pl.position.x, pl.position.y, pl.position.z + pl.osize.dz});
    }

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
                if (!check_support(pos, os, load, problem_.support_rate))
                {
                    continue;
                }

                // 堆码层数 / 单箱承重检查
                if ((problem_.has_max_stack || problem_.has_max_load) &&
                    !check_stack_constraints(pos, os, box.weight.value_or(0.0), load,
                                             box_type_map_))
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
                pl.weight = box.weight;

                load.placements.push_back(pl);
                if (problem_.has_max_stack || problem_.has_max_load)
                {
                    apply_stack_state(pos, os, box.weight.value_or(0.0), load);
                }
                load.used_volume += os.volume();
                if (has_weight_info_)
                {
                    load.total_weight += box.weight.value();
                }
                if (!box.platform.empty())
                {
                    load.platforms.insert(box.platform);
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
