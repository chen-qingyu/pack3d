#pragma once

#include <string>
#include <vector>

#include "types.hpp"

namespace hypercube
{

/// 简单块生成器
/// 对给定的 box_type + 朝向，枚举所有可能的简单块（nx, ny, nz 整数倍堆叠）
class BlockGenerator
{
public:
    explicit BlockGenerator(const std::map<std::string, BoxType>& box_type_map);

    /// 为单个 box_type 生成简单块（约束同平台同分组）
    [[nodiscard]] std::vector<SimpleBlock> generate_for_type(
        const std::string& box_type_id,
        const Size& container_size,
        const std::string& platform,
        const std::string& group,
        int available_count) const;

private:
    const std::map<std::string, BoxType>& box_type_map_;
};

/// 按体积降序排列
void sort_blocks_by_volume_desc(std::vector<SimpleBlock>& blocks) noexcept;

} // namespace hypercube
