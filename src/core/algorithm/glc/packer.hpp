#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../../packer_base.hpp"
#include "heuristic.hpp"

namespace pack3d
{

/// GLC 算法：继承 PackerBase，实现 pack_single() 单容器填充
class GlcPacker : public PackerBase
{
public:
    using PackerBase::PackerBase;

    ContainerLoad pack_single(
        const std::vector<Box>& items,
        const ContainerType& ct,
        const std::vector<Placement>& existing,
        const TenderState& tender,
        bool stop_when_complete = false) override;

private:
    std::optional<glc::PackResult> pack_container(
        const ContainerType* ct,
        const std::vector<const Box*>& boxes,
        const std::vector<Placement>& existing,
        const TenderState& tender) const;
};

} // namespace pack3d
