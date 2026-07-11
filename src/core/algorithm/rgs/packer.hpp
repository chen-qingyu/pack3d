#pragma once

#include <map>
#include <string>
#include <vector>

#include "../../packer_base.hpp"

namespace pack3d
{

/// RGS 算法：继承 PackerBase，实现 pack_single() 单容器填充
class RgsPacker : public PackerBase
{
public:
    RgsPacker(
        const Problem& problem,
        const std::map<std::string, BoxType>& box_type_map,
        const std::map<std::string, ContainerType>& container_type_map,
        const std::map<std::string, Box>& box_map,
        bool has_weight_info);

    ContainerLoad pack_single(
        const std::vector<Box>& items,
        const ContainerType& ct) override;

private:
    const std::map<std::string, ContainerType>& container_type_map_;

    static void rebuild_tracking(ContainerLoad& cl) noexcept;
};

} // namespace pack3d
