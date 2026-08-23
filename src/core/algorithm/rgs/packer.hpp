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

    ContainerLoad pack_single_impl(
        const std::vector<Box>& items,
        const ContainerType& ct,
        const std::vector<Placement>& existing,
        const TenderState& tender,
        bool stop_when_complete) override;

    /// RGS 的 build_ordered_list 已按 route 深度优先排序（深处平台先放），
    /// 不再需要 Packer 层逐平台分桶（避免拆散其时间预算驱动的迭代搜索）。
    bool self_orders_platforms() const override
    {
        return true;
    }
};

} // namespace pack3d
