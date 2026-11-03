// GLC 空间分解不变量：split_space 切分、transfer_space 合并都不能丢体积。
// 这两处丢区域时不会报错、不会崩，只表现为装载率悄悄下降（带尾/带头空着却
// 无空间可放），是最难定位的一类缺陷，所以用体积守恒做硬断言。
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "core/algorithm/glc/space.hpp"

using namespace pack3d;
using namespace pack3d::glc;

namespace
{

int64_t space_volume(const Space& s) noexcept
{
    return static_cast<int64_t>(s.lx) * s.ly * s.lz;
}

int64_t stack_volume(const std::vector<Space>& stack) noexcept
{
    int64_t total = 0;
    for (const auto& s : stack)
    {
        total += space_volume(s);
    }
    return total;
}

} // namespace

TEST_CASE("split_space 切分后子空间与块体积之和守恒", "[glc][space]")
{
    const Space root{{0, 0, 0}, 13500, 2330, 2680, 0, -1, SpaceKind::Root};

    // 覆盖 mx >= my、mx < my、以及块高度吃满（mz = 0，不产生 Z 空间）三种情况
    const std::vector<OrientedSize> blocks = {
        {1200, 1000, 800},
        {1000, 1200, 800},
        {13500, 2330, 2680},
    };

    for (const auto& bs : blocks)
    {
        std::vector<Space> stack;
        split_space(root, bs, stack);

        const int64_t block_volume = static_cast<int64_t>(bs.dx) * bs.dy * bs.dz;
        REQUIRE(stack_volume(stack) == space_volume(root) - block_volume);
    }
}

TEST_CASE("transfer_space 合并时保留碎片溢出的差额区域", "[glc][space]")
{
    // 父空间 13500x2330x800 放入 12500x1000x800 的块：mx(1000) < my(1330)
    // 切出 X(1000x1000x800，贴 x=12500) 与 Y(13500x1330x800，贴 y=1000)，
    // 两者 x 范围相交，合并只扩 y；Y 在 x 方向超出 X 的部分必须另存为独立空间。
    std::vector<Space> stack;
    stack.push_back({{12500, 0, 0}, 1000, 1000, 800, 1, 0, SpaceKind::X});
    stack.push_back({{0, 1000, 0}, 13500, 1330, 800, 2, 0, SpaceKind::Y});

    const int64_t before = stack_volume(stack);
    REQUIRE(transfer_space(stack));

    REQUIRE(stack_volume(stack) == before);
    REQUIRE(stack.size() == 2); // 合并后的主条 + 保留下来的差额区域
}

TEST_CASE("transfer_space 对角空间不合并且体积不变", "[glc][space]")
{
    // X 与 Y 在 x 方向只共端点（无交集），属于对角空间，应各自保留
    std::vector<Space> stack;
    stack.push_back({{1200, 0, 0}, 12300, 2330, 800, 1, 0, SpaceKind::X});
    stack.push_back({{0, 1000, 0}, 1200, 1330, 800, 2, 0, SpaceKind::Y});

    const int64_t before = stack_volume(stack);
    REQUIRE(transfer_space(stack));

    REQUIRE(stack.size() == 2);
    REQUIRE(stack_volume(stack) == before);
}
