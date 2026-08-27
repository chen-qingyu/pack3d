#include "pallet.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace pack3d
{

void from_json(const json& j, PalletType& pt)
{
    j["id"].get_to(pt.id);
    j["sx"].get_to(pt.size.x);
    j["sy"].get_to(pt.size.y);
    j["sz"].get_to(pt.size.z);
    pt.payload = j["payload"].get<double>();
    pt.max_height = j["max_height"].get<int>();
    pt.self_weight = j.value("self_weight", 0.0);
}

ContainerType virtual_container(const PalletType& pt) noexcept
{
    ContainerType ct;
    ct.id = pt.id;
    ct.inner_size = {pt.size.x, pt.size.y, pt.max_height};
    ct.payload = pt.payload;
    return ct;
}

BoxType virtual_box_type(const PalletLoad& p) noexcept
{
    BoxType bt;
    bt.id = p.pallet_id;
    bt.size = {p.type->size.x, p.type->size.y, p.loaded_height + p.type->size.z};
    bt.allowed_orientations = {Orientation::XYZ, Orientation::YXZ};
    // max_stack=[1,1] 限同型不叠托；max_load=[0,0] 限异型不能压上（不缺则异型箱会叠放托盘上）
    bt.max_stack = {std::optional<int>(1), std::optional<int>(1)};
    bt.max_load = {std::optional<double>(0.0), std::optional<double>(0.0)};
    if (p.groups.size() == 1)
    {
        bt.group = *p.groups.begin();
    }
    if (p.platforms.size() == 1)
    {
        bt.platform = *p.platforms.begin();
    }
    return bt;
}

Box virtual_box(const PalletLoad& p) noexcept
{
    Box bx;
    bx.id = p.pallet_id;
    bx.box_type_id = p.pallet_id;
    bx.weight = p.type->self_weight + p.goods_weight;
    bx.platform = (p.platforms.size() == 1) ? *p.platforms.begin() : std::string();
    bx.groups = p.groups;
    return bx;
}

PalletLoad make_pallet_load(const ContainerLoad& load, const PalletType& pt,
                            const std::string& pallet_id) noexcept
{
    PalletLoad p;
    p.pallet_id = pallet_id;
    p.type_id = pt.id;
    p.type = &pt;
    p.placements = load.placements;
    for (const auto& pl : p.placements)
    {
        p.loaded_height = std::max(p.loaded_height, pl.position.z + pl.osize.dz);
        if (pl.weight.has_value())
        {
            p.goods_weight += pl.weight.value();
        }
        for (const auto& group : pl.groups)
        {
            p.groups.insert(group);
        }
        if (!pl.platform.empty())
        {
            p.platforms.insert(pl.platform);
        }
    }
    return p;
}

void to_json(json& j, const PalletLoad& p)
{
    j["pallet_id"] = p.pallet_id;
    j["type_id"] = p.type_id;
    j["sx"] = p.type->size.x;
    j["sy"] = p.type->size.y;
    j["sz"] = p.type->size.z;
    j["payload"] = p.type->payload;
    j["max_height"] = p.type->max_height;
    j["used_height"] = p.loaded_height;
    j["used_weight"] = p.goods_weight;
    // volume_rate = Σ箱子体积 / (sx·sy·max_height)，max_height = 装载限高（不含托盘自身）
    const int64_t usable = static_cast<int64_t>(p.type->size.x) * p.type->size.y *
                           p.type->max_height;
    int64_t vol = 0;
    for (const auto& pl : p.placements)
    {
        vol += pl.osize.volume();
    }
    j["volume_rate"] = usable > 0 ? static_cast<double>(vol) / static_cast<double>(usable) : 0.0;
    j["groups"] = std::vector<std::string>(p.groups.begin(), p.groups.end());
    j["platforms"] = std::vector<std::string>(p.platforms.begin(), p.platforms.end());
    json placements = json::array();
    for (const auto& pl : p.placements)
    {
        placements.push_back(json(pl));
    }
    j["placements"] = std::move(placements);
}

} // namespace pack3d
