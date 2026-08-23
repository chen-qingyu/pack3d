#pragma once

#include "../../packer_base.hpp"

namespace pack3d
{

/// BSG 算法：继承 PackerBase，实现 pack_single() 单容器填充
class BsgPacker : public PackerBase
{
public:
    using PackerBase::PackerBase;

    ContainerLoad pack_single_impl(
        const std::vector<Box>& items,
        const ContainerType& ct,
        const std::vector<Placement>& existing,
        const TenderState& tender,
        bool stop_when_complete) override;
};

} // namespace pack3d
