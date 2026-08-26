#include "space.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "../../constraints.hpp"

namespace pack3d::glc
{

namespace
{

int64_t next_space_id() noexcept
{
    static int64_t next_id = 1;
    return next_id++;
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

    // GLC 切分策略：比较 mx/my，选择一个地面剩余空间扩展为整条带状空间。
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
    // 仅检查栈顶：Z 空间直接丢弃，X/Y 尝试与紧邻次栈顶合并
    if (stack.empty())
    {
        return false;
    }

    Space& top = stack.back();

    // Z 空间从不参与同级合并，直接丢弃
    if (top.kind == SpaceKind::Z)
    {
        stack.pop_back();
        return true;
    }

    // 根空间（初始空间/雕刻产物）不参与同级合并：无可行块时返回 false，由调用方 pop 丢弃
    if (top.kind == SpaceKind::Root)
    {
        return false;
    }

    // X/Y 碎片：没有兄弟则丢弃
    if (stack.size() < 2)
    {
        stack.pop_back();
        return true;
    }

    Space& next = stack[stack.size() - 2];

    // 兄弟必须来自同一次划分、同一高度层
    if (top.parent_id < 0 || top.parent_id != next.parent_id ||
        top.pos.z != next.pos.z || top.lz != next.lz)
    {
        stack.pop_back();
        return true;
    }

    // 合并：栈顶碎片并入次栈顶主条
    // 入栈顺序保证了碎片总是在栈顶（split_space 将较窄的碎条最后压入）
    if (top.kind == SpaceKind::X && next.kind == SpaceKind::Y)
    {
        // 仅当两者在 Y 方向有交集时才合并；否则为对角空间，保留各自独立
        int32_t y_overlap_start = std::max(top.pos.y, next.pos.y);
        int32_t y_overlap_end = std::min(top.pos.y + top.ly, next.pos.y + next.ly);
        if (y_overlap_end <= y_overlap_start)
        {
            std::swap(top, next); // 对角空间：交换顺序，让大概率可用的空间先处理
            return true;
        }
        // X 碎片并入 Y 主条：扩展 Y 的 x 范围
        int32_t new_lx = std::max(next.pos.x + next.lx, top.pos.x + top.lx) - next.pos.x;
        next.lx = new_lx;
        next.parent_id = -1; // 标记已合并，防止后续误匹配
        stack.pop_back();
        return true;
    }
    if (top.kind == SpaceKind::Y && next.kind == SpaceKind::X)
    {
        // 仅当两者在 X 方向有交集时才合并
        int32_t x_overlap_start = std::max(top.pos.x, next.pos.x);
        int32_t x_overlap_end = std::min(top.pos.x + top.lx, next.pos.x + next.lx);
        if (x_overlap_end <= x_overlap_start)
        {
            std::swap(top, next); // 对角空间：交换顺序
            return true;
        }
        // Y 碎片并入 X 主条：扩展 X 的 y 范围
        int32_t new_ly = std::max(next.pos.y + next.ly, top.pos.y + top.ly) - next.pos.y;
        next.ly = new_ly;
        next.parent_id = -1;
        stack.pop_back();
        return true;
    }

    // 同次划分但类型异常，丢弃
    stack.pop_back();
    return true;
}

namespace
{

// 从一个 AABB 盒子（与空间相交时）生成 6-slab 完整分解，覆盖 space - box
void carve_box_into(const Space& sp,
                    int32_t bx, int32_t by, int32_t bz,
                    int32_t bdx, int32_t bdy, int32_t bdz,
                    std::vector<Space>& next) noexcept
{
    int32_t sx = sp.pos.x, sy = sp.pos.y, sz = sp.pos.z;
    int32_t ex = sx + sp.lx, ey = sy + sp.ly, ez = sz + sp.lz;
    int32_t ox_min = std::max(sx, bx), ox_max = std::min(ex, bx + bdx);
    int32_t oy_min = std::max(sy, by), oy_max = std::min(ey, by + bdy);
    int32_t oz_min = std::max(sz, bz), oz_max = std::min(ez, bz + bdz);

    // 不相交：保留原空间
    if (ox_min >= ox_max || oy_min >= oy_max || oz_min >= oz_max)
    {
        next.push_back(sp);
        return;
    }

    // 6-slab 分解：左右前后下上，完整覆盖 space - box（carve_out_space 复用本实现）
    auto push = [&](int32_t x, int32_t y, int32_t z,
                    int32_t lx, int32_t ly, int32_t lz)
    {
        if (lx > 0 && ly > 0 && lz > 0)
        {
            Space s;
            s.pos = {x, y, z};
            s.lx = lx;
            s.ly = ly;
            s.lz = lz;
            s.id = next_space_id();
            s.parent_id = sp.id;
            s.kind = SpaceKind::Root;
            next.push_back(s);
        }
    };
    push(sx, sy, sz, ox_min - sx, sp.ly, sp.lz);                                 // 左
    push(ox_max, sy, sz, ex - ox_max, sp.ly, sp.lz);                             // 右
    push(ox_min, sy, sz, ox_max - ox_min, oy_min - sy, sp.lz);                   // 前
    push(ox_min, oy_max, sz, ox_max - ox_min, ey - oy_max, sp.lz);               // 后
    push(ox_min, oy_min, sz, ox_max - ox_min, oy_max - oy_min, oz_min - sz);     // 下
    push(ox_min, oy_min, oz_max, ox_max - ox_min, oy_max - oy_min, ez - oz_max); // 上
}

// 从空间栈中挖掉一个 AABB 盒子
void carve_box(std::vector<Space>& stack,
               int32_t x, int32_t y, int32_t z,
               int32_t dx, int32_t dy, int32_t dz) noexcept
{
    std::vector<Space> next;
    for (const auto& sp : stack)
    {
        carve_box_into(sp, x, y, z, dx, dy, dz, next);
    }
    stack = std::move(next);
}

} // namespace

void carve_out_space(const Space& space,
                     const Placement& pl,
                     std::vector<Space>& stack) noexcept
{
    // 从 space 中挖掉 pl 占用的区域（pl 完全包含在 space 内，但不在角落）
    // 6-slab 完整分解 space - pl，覆盖全部剩余空间（不丢对角空间）
    std::vector<Space> next;
    carve_box_into(space,
                   pl.position.x, pl.position.y, pl.position.z,
                   pl.osize.dx, pl.osize.dy, pl.osize.dz,
                   next);

    // 体积降序排序后入栈 → 小块最后压栈（栈顶），优先处理
    std::sort(next.begin(), next.end(),
              [](const Space& a, const Space& b) noexcept
              {
                  return a.lx * static_cast<int64_t>(a.ly) * a.lz >
                         b.lx * static_cast<int64_t>(b.ly) * b.lz;
              });
    for (auto& s : next)
    {
        stack.push_back(std::move(s));
    }
}

void carve_obstacles(std::vector<Space>& stack,
                     const std::vector<Obstacle>& obstacles) noexcept
{
    for (const auto& o : obstacles)
    {
        carve_box(stack, o.x, o.y, o.z, o.dx, o.dy, o.dz);
        if (stack.empty())
        {
            return;
        }
    }
}

void carve_facets(std::vector<Space>& stack,
                  const Size& container_size,
                  const std::vector<Facet>& facets) noexcept
{
    // 仅雕刻"禁区覆盖原点"的斜面（两正截距，贴原点角）：否则初始空间 min corner 在
    // 禁区、所有块被 check_facet 拒、空间被 transfer 丢弃，导致零装载。其余斜面不雕刻，
    // 由 check_block_feasible 的 check_facet 逐箱兜底（阶梯碎片会切碎空间栈、降低装载）。
    for (const auto& f : facets)
    {
        if (!facet_covers_origin(f))
        {
            continue;
        }
        for (const auto& s : facet_staircase(f, container_size, FACET_STAIR_STEPS))
        {
            carve_box(stack, s.x, s.y, s.z, s.dx, s.dy, s.dz);
            if (stack.empty())
            {
                return;
            }
        }
    }
}

} // namespace pack3d::glc
