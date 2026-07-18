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
    using PackerBase::PackerBase;

    ContainerLoad pack_single(
        const std::vector<Box>& items,
        const ContainerType& ct,
        bool stop_when_complete = false) override;
};

} // namespace pack3d
