#pragma once

#include <map>
#include <string>
#include <vector>

#include "../../types.hpp"

namespace hypercube::rgs
{

// RGS 多 ULD 主入口
class Packer
{
public:
    Packer(
        const Problem& problem,
        const std::map<std::string, BoxType>& box_type_map,
        const std::map<std::string, ContainerType>& container_type_map,
        const std::map<std::string, Box>& box_map,
        bool has_weight_info);

    [[nodiscard]] Solution pack();

private:
    const Problem& problem_;
    const std::map<std::string, BoxType>& box_type_map_;
    const std::map<std::string, ContainerType>& container_type_map_;
    const std::map<std::string, Box>& box_map_;
    bool has_weight_info_;

    // 选下一个 ULD 类型（Alg7，最稀缺优先）
    [[nodiscard]] const ContainerType* select_next_uld(
        const std::vector<Box>& remaining,
        const std::map<std::string, int>& container_usage) const noexcept;

    // Alg5: 单 ULD 的 RGS 多起点搜索
    [[nodiscard]] ContainerLoad rgs_single_uld(
        const std::vector<Box>& items,
        const ContainerType& ctype,
        double penalty_denom) const noexcept;

    // 根据 placements 重建 ContainerLoad 的追踪字段
    static void rebuild_tracking(ContainerLoad& cl) noexcept;
};

} // namespace hypercube::rgs
