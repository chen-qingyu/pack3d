#pragma once

#include <string>

#include "io.hpp"
#include "types.hpp"

namespace pack3d
{

// PalletType JSON 解析（schema 已保证必需字段与范围）
void from_json(const json& j, PalletType& pt);

// PalletLoad JSON 序列化（result.pallets 数组元素）
void to_json(json& j, const PalletLoad& p);

/// 托盘虚拟容器：inner_size = 托盘平面 × max_height（装载限高，不含托盘自身），payload = 货物载重
[[nodiscard]] ContainerType virtual_container(const PalletType& pt) noexcept;

/// 虚拟托盘箱型：size = 托盘平面 × (loaded_height + sz)，仅 XY 平面旋转，max_stack=[1,1]
[[nodiscard]] BoxType virtual_box_type(const PalletLoad& p) noexcept;

/// 虚拟托盘箱：重量 = 自重 + 货物重；groups = 托盘的完整分组集合
[[nodiscard]] Box virtual_box(const PalletLoad& p) noexcept;

/// 从 pack_single 结果构建 PalletLoad（计算 loaded_height/goods_weight/groups/platforms）
[[nodiscard]] PalletLoad make_pallet_load(const ContainerLoad& load,
                                          const PalletType& pt,
                                          const std::string& pallet_id) noexcept;

} // namespace pack3d
