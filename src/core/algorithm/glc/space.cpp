#include "space.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

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

    // 根空间永不丢弃
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

void carve_out_space(const Space& space,
                     const Placement& pl,
                     std::vector<Space>& stack) noexcept
{
    // 从 space 中挖掉 pl 占用的区域
    // pl 完全包含在 space 内，但不在角落
    // 生成 6 个方向的子空间，按体积降序入栈（小块在栈顶优先处理）

    int32_t sx = space.pos.x, sy = space.pos.y, sz = space.pos.z;
    int32_t slx = space.lx, sly = space.ly, slz = space.lz;
    int32_t px = pl.position.x, py = pl.position.y, pz = pl.position.z;
    int32_t plx = pl.osize.dx, ply = pl.osize.dy, plz = pl.osize.dz;

    struct Candidate
    {
        Space sp;
        int64_t vol;
    };
    std::vector<Candidate> cands;

    // 上方 (Z+)
    if (sz + slz > pz + plz)
    {
        Space s;
        s.pos = {px, py, pz + plz};
        s.lx = plx;
        s.ly = ply;
        s.lz = sz + slz - pz - plz;
        s.id = next_space_id();
        s.parent_id = space.id;
        s.kind = SpaceKind::Z;
        cands.push_back({s, s.lx * static_cast<int64_t>(s.ly) * s.lz});
    }

    // 下方 (Z-)
    if (pz > sz)
    {
        Space s;
        s.pos = {sx, sy, sz};
        s.lx = slx;
        s.ly = sly;
        s.lz = pz - sz;
        s.id = next_space_id();
        s.parent_id = space.id;
        s.kind = SpaceKind::Z;
        cands.push_back({s, s.lx * static_cast<int64_t>(s.ly) * s.lz});
    }

    // 右方 (X+)
    if (sx + slx > px + plx)
    {
        Space s;
        s.pos = {px + plx, py, pz};
        s.lx = sx + slx - px - plx;
        s.ly = ply;
        s.lz = plz;
        s.id = next_space_id();
        s.parent_id = space.id;
        s.kind = SpaceKind::X;
        cands.push_back({s, s.lx * static_cast<int64_t>(s.ly) * s.lz});
    }

    // 左方 (X-)
    if (px > sx)
    {
        Space s;
        s.pos = {sx, py, pz};
        s.lx = px - sx;
        s.ly = ply;
        s.lz = plz;
        s.id = next_space_id();
        s.parent_id = space.id;
        s.kind = SpaceKind::X;
        cands.push_back({s, s.lx * static_cast<int64_t>(s.ly) * s.lz});
    }

    // 后方 (Y+)
    if (sy + sly > py + ply)
    {
        Space s;
        s.pos = {px, py + ply, pz};
        s.lx = plx;
        s.ly = sy + sly - py - ply;
        s.lz = plz;
        s.id = next_space_id();
        s.parent_id = space.id;
        s.kind = SpaceKind::Y;
        cands.push_back({s, s.lx * static_cast<int64_t>(s.ly) * s.lz});
    }

    // 前方 (Y-)
    if (py > sy)
    {
        Space s;
        s.pos = {px, sy, pz};
        s.lx = plx;
        s.ly = py - sy;
        s.lz = plz;
        s.id = next_space_id();
        s.parent_id = space.id;
        s.kind = SpaceKind::Y;
        cands.push_back({s, s.lx * static_cast<int64_t>(s.ly) * s.lz});
    }

    // 体积降序排列 → 小块先压栈（后弹出，优先处理）
    std::sort(cands.begin(), cands.end(),
              [](const Candidate& a, const Candidate& b) noexcept
              { return a.vol < b.vol; });

    for (auto& c : cands)
    {
        stack.push_back(std::move(c.sp));
    }
}

} // namespace pack3d::glc
