#include "space.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace hypercube
{

void split_space(const Space& space, const OrientedSize& block_osize,
                 std::vector<Space>& stack) noexcept
{
    int32_t dx = block_osize.dx;
    int32_t dy = block_osize.dy;
    int32_t dz = block_osize.dz;

    // 三个子空间：
    // spaceZ: 块上方（高度方向剩余）
    // spaceX: 块右方（长度方向剩余）
    // spaceY: 块后方（宽度方向剩余）
    Space spaceZ;
    spaceZ.pos = {space.pos.x, space.pos.y, space.pos.z + dz};
    spaceZ.lx = space.lx;
    spaceZ.ly = space.ly;
    spaceZ.lz = space.lz - dz;

    Space spaceX;
    spaceX.pos = {space.pos.x + dx, space.pos.y, space.pos.z};
    spaceX.lx = space.lx - dx;
    spaceX.ly = dy;
    spaceX.lz = dz;

    Space spaceY;
    spaceY.pos = {space.pos.x, space.pos.y + dy, space.pos.z};
    spaceY.lx = dx;
    spaceY.ly = space.ly - dy;
    spaceY.lz = dz;

    // 按原始 MLHS 策略决定入栈顺序
    // 优先让更大的一面留在栈顶（更容易被后续块利用）
    if (space.lx >= space.ly)
    {
        if (spaceZ.lz > 0)
            stack.push_back(spaceZ);
        if (spaceX.lx > 0 && spaceX.ly > 0 && spaceX.lz > 0)
            stack.push_back(spaceX);
        if (spaceY.lx > 0 && spaceY.ly > 0 && spaceY.lz > 0)
            stack.push_back(spaceY);
    }
    else
    {
        if (spaceZ.lz > 0)
            stack.push_back(spaceZ);
        if (spaceY.lx > 0 && spaceY.ly > 0 && spaceY.lz > 0)
            stack.push_back(spaceY);
        if (spaceX.lx > 0 && spaceX.ly > 0 && spaceX.lz > 0)
            stack.push_back(spaceX);
    }
}

bool transfer_space(std::vector<Space>& stack) noexcept
{
    // 简化版 TransferSpace：
    // 当前栈顶空间没有可行块时，尝试将其与栈中可能合并的兄弟空间合并
    // 这是一个启发式回收，不保证完全覆盖原始 MLHS 的所有回收场景
    //
    // 当前实现：如果栈顶空间在某个维度上长度为 0（退化），直接丢弃
    // 更完善的实现需要跟踪空间的划分来源（parent），
    // 将可转移区域合并回兄弟空间

    if (stack.empty())
    {
        return false;
    }

    const auto& top = stack.back();

    // 检查是否退化（零体积或负维度）
    if (top.lx <= 0 || top.ly <= 0 || top.lz <= 0)
    {
        stack.pop_back();
        return true;
    }

    // 尝试与栈中同面相邻的空间合并（简单合并）
    // 遍历栈中其他空间，看是否有可以与 top 合并的
    for (size_t i = 0; i + 1 < stack.size(); ++i)
    {
        auto& other = stack[i];

        // 检查是否在 Y 方向相邻（同 X、同 Z）
        if (other.pos.x == top.pos.x &&
            other.pos.z == top.pos.z &&
            other.lx == top.lx &&
            other.lz == top.lz &&
            (other.pos.y + other.ly == top.pos.y ||
             top.pos.y + top.ly == other.pos.y))
        {
            // 合并
            int32_t new_y = std::min(other.pos.y, top.pos.y);
            int32_t new_ly = std::max(other.pos.y + other.ly, top.pos.y + top.ly) - new_y;
            other.pos.y = new_y;
            other.ly = new_ly;
            stack.pop_back();
            return true;
        }

        // 检查是否在 X 方向相邻（同 Y、同 Z）
        if (other.pos.y == top.pos.y &&
            other.pos.z == top.pos.z &&
            other.ly == top.ly &&
            other.lz == top.lz &&
            (other.pos.x + other.lx == top.pos.x ||
             top.pos.x + top.lx == other.pos.x))
        {
            int32_t new_x = std::min(other.pos.x, top.pos.x);
            int32_t new_lx = std::max(other.pos.x + other.lx, top.pos.x + top.lx) - new_x;
            other.pos.x = new_x;
            other.lx = new_lx;
            stack.pop_back();
            return true;
        }
    }

    return false;
}

} // namespace hypercube
