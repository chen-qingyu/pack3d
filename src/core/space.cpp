#include "space.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace hypercube
{

namespace
{

int64_t next_space_id() noexcept
{
    static int64_t next_id = 1;
    return next_id++;
}

bool same_layer_siblings(const Space& a, const Space& b) noexcept
{
    return a.parent_id >= 0 &&
           a.parent_id == b.parent_id &&
           a.pos.z == b.pos.z &&
           a.lz == b.lz;
}

} // namespace

void split_space(const Space& space, const OrientedSize& block_osize,
                 std::vector<Space>& stack) noexcept
{
    int32_t dx = block_osize.dx;
    int32_t dy = block_osize.dy;
    int32_t dz = block_osize.dz;
    int32_t mx = space.lx - dx;
    int32_t my = space.ly - dy;
    int32_t mz = space.lz - dz;

    // 三个子空间：
    // spaceZ: 块上方（高度方向剩余）
    // spaceX: 块右方（长度方向剩余）
    // spaceY: 块后方（宽度方向剩余）
    Space spaceZ;
    spaceZ.pos = {space.pos.x, space.pos.y, space.pos.z + dz};
    spaceZ.lx = space.lx;
    spaceZ.ly = space.ly;
    spaceZ.lz = mz;
    spaceZ.id = next_space_id();
    spaceZ.parent_id = space.id;
    spaceZ.kind = SpaceKind::Z;

    Space spaceX;
    spaceX.pos = {space.pos.x + dx, space.pos.y, space.pos.z};
    spaceX.lx = mx;
    spaceX.lz = dz;
    spaceX.id = next_space_id();
    spaceX.parent_id = space.id;
    spaceX.kind = SpaceKind::X;

    Space spaceY;
    spaceY.pos = {space.pos.x, space.pos.y + dy, space.pos.z};
    spaceY.lz = dz;
    spaceY.id = next_space_id();
    spaceY.parent_id = space.id;
    spaceY.kind = SpaceKind::Y;

    // MLHS 切分策略：比较 mx/my，选择一个地面剩余空间扩展为整条带状空间。
    if (mx >= my)
    {
        spaceX.ly = space.ly;
        spaceY.lx = dx;
        spaceY.ly = my;

        if (spaceZ.lz > 0)
        {
            stack.push_back(spaceZ);
        }
        if (spaceX.lx > 0 && spaceX.ly > 0 && spaceX.lz > 0)
        {
            stack.push_back(spaceX);
        }
        if (spaceY.lx > 0 && spaceY.ly > 0 && spaceY.lz > 0)
        {
            stack.push_back(spaceY);
        }
    }
    else
    {
        spaceX.ly = dy;
        spaceY.lx = space.lx;
        spaceY.ly = my;

        if (spaceZ.lz > 0)
        {
            stack.push_back(spaceZ);
        }
        if (spaceY.lx > 0 && spaceY.ly > 0 && spaceY.lz > 0)
        {
            stack.push_back(spaceY);
        }
        if (spaceX.lx > 0 && spaceX.ly > 0 && spaceX.lz > 0)
        {
            stack.push_back(spaceX);
        }
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

    for (size_t i = 0; i + 1 < stack.size(); ++i)
    {
        auto& other = stack[i];

        if (same_layer_siblings(other, top))
        {
            if (top.kind == SpaceKind::X && other.kind == SpaceKind::Y)
            {
                int32_t right = std::max(other.pos.x + other.lx, top.pos.x + top.lx);
                other.lx = right - other.pos.x;
                stack.pop_back();
                return true;
            }
            if (top.kind == SpaceKind::Y && other.kind == SpaceKind::X)
            {
                int32_t back = std::max(other.pos.y + other.ly, top.pos.y + top.ly);
                other.ly = back - other.pos.y;
                stack.pop_back();
                return true;
            }
        }

        // 退化到一般相邻合并，保留原有启发式。
        if (other.pos.x == top.pos.x &&
            other.pos.z == top.pos.z &&
            other.lx == top.lx &&
            other.lz == top.lz &&
            (other.pos.y + other.ly == top.pos.y ||
             top.pos.y + top.ly == other.pos.y))
        {
            int32_t new_y = std::min(other.pos.y, top.pos.y);
            int32_t new_ly = std::max(other.pos.y + other.ly, top.pos.y + top.ly) - new_y;
            other.pos.y = new_y;
            other.ly = new_ly;
            stack.pop_back();
            return true;
        }

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
