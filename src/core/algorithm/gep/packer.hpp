#pragma once

#include <map>
#include <string>
#include <vector>

#include "../../packer_base.hpp"

namespace pack3d
{

/// GEP 算法：极点优先填充（单容器，顺序贪心）
class GepPacker : public PackerBase
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
