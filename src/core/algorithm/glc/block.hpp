#pragma once

#include <string>
#include <vector>

#include "../../types.hpp"

namespace hypercube::glc
{

// 简单块：同种箱子、同朝向、整数倍堆叠
struct SimpleBlock
{
    std::string box_type_id;
    Orientation orientation = Orientation::XYZ;
    int nx = 0, ny = 0, nz = 0;
    int box_count = 0;    // nx * ny * nz
    OrientedSize osize;   // 块的外包尺寸
    std::string platform; // 块内箱子共享的平台（空表示无）
    std::string group;    // 块内箱子共享的分组（空表示无）

    int64_t volume() const noexcept
    {
        return osize.volume();
    }
};

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

} // namespace hypercube::glc
