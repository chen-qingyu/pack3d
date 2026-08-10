#include <algorithm>
#include <array>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/algorithm/glc/space.hpp"
#include "core/constraints.hpp"
#include "core/objectives.hpp"
#include "core/types.hpp"

using namespace pack3d;

TEST_CASE("Size::orient 产生正确的尺寸", "[core]")
{
    Size base{500, 400, 300};

    auto r1 = base.orient(Orientation::XYZ);
    REQUIRE(r1.dx == 500);
    REQUIRE(r1.dy == 400);
    REQUIRE(r1.dz == 300);

    auto r2 = base.orient(Orientation::YXZ);
    REQUIRE(r2.dx == 400);
    REQUIRE(r2.dy == 500);
    REQUIRE(r2.dz == 300);

    auto r3 = base.orient(Orientation::ZYX);
    REQUIRE(r3.dx == 300);
    REQUIRE(r3.dy == 400);
    REQUIRE(r3.dz == 500);
}

TEST_CASE("check_boundary 拒绝越界", "[core]")
{
    ContainerType ct;
    ct.inner_size = {1000, 1000, 1000};
    ct.max_weight = 1000.0;

    REQUIRE(check_boundary(ct, {0, 0, 0}, {500, 500, 500}));
    REQUIRE(check_boundary(ct, {500, 500, 500}, {500, 500, 500}));
    REQUIRE_FALSE(check_boundary(ct, {501, 0, 0}, {500, 500, 500}));
    REQUIRE_FALSE(check_boundary(ct, {0, -1, 0}, {500, 500, 500}));
    REQUIRE_FALSE(check_boundary(ct, {0, 0, 0}, {1001, 500, 500}));
}

TEST_CASE("check_overlap 检测碰撞", "[core]")
{
    std::vector<Placement> exists = {{"", "t", "", {50, 50, 50}, Orientation::XYZ, {100, 100, 100}}};

    REQUIRE(check_overlap({0, 0, 0}, {100, 100, 100}, exists));

    exists[0].position = {200, 0, 0};
    REQUIRE_FALSE(check_overlap({0, 0, 0}, {100, 100, 100}, exists));

    exists[0].position = {0, 200, 0};
    REQUIRE_FALSE(check_overlap({0, 0, 0}, {100, 100, 100}, exists));
}

TEST_CASE("check_support 在地板上总是通过", "[core]")
{
    ContainerLoad load;
    load.type_id = "test";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    REQUIRE(check_support({0, 0, 0}, {100, 100, 100}, load, 1.0));
    REQUIRE(check_support({0, 0, 0}, {100, 100, 100}, load, 0.0));
}

TEST_CASE("check_support 部分支撑", "[core]")
{
    ContainerLoad load;
    load.type_id = "test";
    ContainerType ct{{}, {1000, 1000, 1000}, 10000.0};
    load.type = &ct;

    load.placements.push_back({"", "bt1", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}});
    load.used_volume = 100 * 100 * 100;
    load.total_weight = 100.0;

    REQUIRE(check_support({0, 0, 100}, {100, 100, 100}, load, 0.0));
    REQUIRE(check_support({0, 0, 100}, {100, 100, 100}, load, 1.0));
    REQUIRE(check_support({0, 0, 100}, {200, 100, 100}, load, 0.5));
    REQUIRE_FALSE(check_support({0, 0, 100}, {200, 100, 100}, load, 0.6));
}

// 四角均被支撑但面积占比不足时，高 support_rate 必须拒绝（回归：删除四角快通道后）
TEST_CASE("check_support 四角支撑但面积不足被拒绝", "[core]")
{
    ContainerLoad load;
    load.type_id = "test";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    // 4 个 10x10 角块在四角，顶面 z=50
    load.placements.push_back({"", "bt", "", {0, 0, 0}, Orientation::XYZ, {10, 10, 50}});
    load.placements.push_back({"", "bt", "", {90, 0, 0}, Orientation::XYZ, {10, 10, 50}});
    load.placements.push_back({"", "bt", "", {0, 90, 0}, Orientation::XYZ, {10, 10, 50}});
    load.placements.push_back({"", "bt", "", {90, 90, 0}, Orientation::XYZ, {10, 10, 50}});

    // 100x100 大箱：四角全被支撑，但支撑面积仅 4%
    REQUIRE_FALSE(check_support({0, 0, 50}, {100, 100, 100}, load, 0.9));
    REQUIRE(check_support({0, 0, 50}, {100, 100, 100}, load, 0.03));
}

TEST_CASE("平台数量限制约束", "[core]")
{
    ContainerLoad load;
    load.type_id = "test";
    ContainerType ct{};
    ct.inner_size = {1000, 1000, 1000};
    ct.max_weight = 10000.0;
    load.type = &ct;
    load.platforms.insert("A");

    REQUIRE_FALSE(check_platform_limit(load, "B", 1));
    REQUIRE(check_platform_limit(load, "A", 1));
    REQUIRE(check_platform_limit(load, "B", 0));
}

TEST_CASE("路线约束：X 重叠但 Y 分隔允许", "[core][route]")
{
    ContainerLoad load;
    load.type_id = "t";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    RouteOrder route{{"A", "B"}};
    route.index_of = {{"A", 0}, {"B", 1}};

    // 先放 A（近门）到 (0,0,0)
    load.placements.push_back({"", "", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}, "A"});

    // B 的 X 与 A 重叠，但 Y 完全分离 → 允许
    REQUIRE(check_route_order(load, "B", {0, 200, 0}, {100, 100, 100}, route));
}

TEST_CASE("路线约束：X 重叠但 Z 分隔允许", "[core][route]")
{
    ContainerLoad load;
    load.type_id = "t";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    RouteOrder route{{"A", "B"}};
    route.index_of = {{"A", 0}, {"B", 1}};

    load.placements.push_back({"", "", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}, "A"});

    // B 与 A 在 Z 上完全分离（B 在上方但未直接叠压）→ 允许
    REQUIRE(check_route_order(load, "B", {0, 0, 200}, {100, 100, 100}, route));
}

TEST_CASE("路线约束：YZ 重叠时先卸平台必须在近门处", "[core][route]")
{
    ContainerLoad load;
    load.type_id = "t";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    RouteOrder route{{"A", "B"}};
    route.index_of = {{"A", 0}, {"B", 1}};

    // B（后卸）在深处 (200,0,0)
    load.placements.push_back({"", "", "", {200, 0, 0}, Orientation::XYZ, {100, 100, 100}, "B"});

    // A 在 YZ 上与 B 重叠，但 X 在 B 左侧（深处）→ 拒绝（A 应先卸，必须在近门处）
    REQUIRE_FALSE(check_route_order(load, "A", {0, 0, 0}, {100, 100, 100}, route));
    // A 完全在 B 右侧（近门）→ 允许
    REQUIRE(check_route_order(load, "A", {300, 0, 0}, {100, 100, 100}, route));
}

TEST_CASE("路线约束：XY 重叠时后卸平台不能压住先卸平台", "[core][route]")
{
    ContainerLoad load;
    load.type_id = "t";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    RouteOrder route{{"A", "B"}};
    route.index_of = {{"A", 0}, {"B", 1}};

    // A（先卸）在 (0, 0, 100)，B（后卸）在 (0, 0, 0)
    // XY 重叠，B.z=0 ≤ A.z=100 → B 没压住 A → 允许
    load.placements.push_back({"", "", "", {0, 0, 100}, Orientation::XYZ, {100, 100, 50}, "A"});
    REQUIRE(check_route_order(load, "B", {0, 0, 0}, {100, 100, 100}, route));
}

TEST_CASE("路线约束：后卸平台压住先卸平台被拒绝", "[core][route]")
{
    ContainerLoad load;
    load.type_id = "t";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    RouteOrder route{{"A", "B"}};
    route.index_of = {{"A", 0}, {"B", 1}};

    // A（先卸）在 (0,0,0) 高 100，B（后卸）在 (0,0,100) 直接叠在 A 上
    load.placements.push_back({"", "", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}, "A"});
    // B 直接压在 A 上方 → 拒绝
    REQUIRE_FALSE(check_route_order(load, "B", {0, 0, 100}, {100, 100, 50}, route));
}

TEST_CASE("ObjectiveVector 字典序比较", "[core]")
{
    ObjectiveVector a{2, 3, 0.8, 5};
    ObjectiveVector b{2, 3, 0.8, 5};
    REQUIRE(a == b);
    REQUIRE_FALSE(a.is_better_than(b));

    ObjectiveVector fewer_containers{1, 2, 0.9, 3};
    REQUIRE(fewer_containers.is_better_than(a));

    ObjectiveVector fewer_platforms{2, 2, 0.8, 5};
    REQUIRE(fewer_platforms.is_better_than(a));

    ObjectiveVector higher_rate{2, 3, 0.9, 5};
    REQUIRE(higher_rate.is_better_than(a));
}

namespace
{
BoxType make_stack_bt(const std::string& id, std::optional<int> max_stack,
                      std::optional<double> max_load = std::nullopt)
{
    BoxType bt;
    bt.id = id;
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    if (max_stack.has_value())
    {
        bt.max_stack = {max_stack.value()};
    }
    if (max_load.has_value())
    {
        bt.max_load = {max_load.value()};
    }
    return bt;
}

ContainerLoad make_stack_load()
{
    static ContainerType ct{{}, {1000, 1000, 1000}};
    ContainerLoad load;
    load.type_id = "t";
    load.type = &ct;
    return load;
}
} // namespace

TEST_CASE("max_stack: 同型柱限层", "[core][stack]")
{
    auto bt = make_stack_bt("bt", 2);
    std::map<std::string, BoxType> btm = {{"bt", bt}};
    auto load = make_stack_load();

    load.placements.push_back({"", "bt", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 0}, {100, 100, 100}, 0.0, load);
    REQUIRE(load.placements.back().stack_level == 1);

    // 第二层 level 2 <= max_stack 2
    REQUIRE(check_stack_constraints({0, 0, 100}, {100, 100, 100}, 0.0, load, btm));
    load.placements.push_back({"", "bt", "", {0, 0, 100}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 100}, {100, 100, 100}, 0.0, load);
    REQUIRE(load.placements.back().stack_level == 2);

    // 第三层 level 3 > max_stack 2 → 拒绝
    REQUIRE_FALSE(check_stack_constraints({0, 0, 200}, {100, 100, 100}, 0.0, load, btm));
}

TEST_CASE("max_stack: 异构最弱箱限制", "[core][stack]")
{
    auto strong = make_stack_bt("strong", 5);
    auto weak = make_stack_bt("weak", 2);
    std::map<std::string, BoxType> btm = {{"strong", strong}, {"weak", weak}};
    auto load = make_stack_load();

    load.placements.push_back({"", "strong", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 0}, {100, 100, 100}, 0.0, load);
    // 中层 weak 放在 strong 上：level 2 <= strong.max_stack 5
    REQUIRE(check_stack_constraints({0, 0, 100}, {100, 100, 100}, 0.0, load, btm));
    load.placements.push_back({"", "weak", "", {0, 0, 100}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 100}, {100, 100, 100}, 0.0, load);
    REQUIRE(load.placements.back().stack_level == 2);
    // 顶层放在 weak 上：level 3 > weak.max_stack 2 → 拒绝（weak 是最弱箱）
    REQUIRE_FALSE(check_stack_constraints({0, 0, 200}, {100, 100, 100}, 0.0, load, btm));
}

TEST_CASE("max_load: 面积加权累计承重", "[core][stack]")
{
    auto bt = make_stack_bt("bt", std::nullopt, 100.0);
    std::map<std::string, BoxType> btm = {{"bt", bt}};
    auto load = make_stack_load();

    // 底座 200x200
    load.placements.push_back({"", "bt", "", {0, 0, 0}, Orientation::XYZ, {200, 200, 100}});
    apply_stack_state({0, 0, 0}, {200, 200, 100}, 0.0, load);
    // box2 200x100 完全在底座上：承重 100
    REQUIRE(check_stack_constraints({0, 0, 100}, {200, 100, 100}, 100.0, load, btm));
    load.placements.push_back({"", "bt", "", {0, 0, 100}, Orientation::XYZ, {200, 100, 100}});
    apply_stack_state({0, 0, 100}, {200, 100, 100}, 100.0, load);
    REQUIRE(load.placements[0].supported_load == Catch::Approx(100.0));
    // box3 200x100 并排（y=100）：底座累计 200 > max_load 100 → 拒绝
    REQUIRE_FALSE(check_stack_constraints({0, 0, 100}, {200, 100, 100}, 100.0, load, btm));
}

TEST_CASE("max_load: 悬空半面积仍 100% 承重", "[core][stack]")
{
    auto bt = make_stack_bt("bt", std::nullopt, 100.0);
    std::map<std::string, BoxType> btm = {{"bt", bt}};
    auto load = make_stack_load();

    // 底座 100x100
    load.placements.push_back({"", "bt", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 0}, {100, 100, 100}, 0.0, load);
    // 上箱 200x100，一半在底座上、一半悬空 → 承重仍为 100%
    REQUIRE(check_stack_constraints({0, 0, 100}, {200, 100, 100}, 100.0, load, btm));
    load.placements.push_back({"", "bt", "", {0, 0, 100}, Orientation::XYZ, {200, 100, 100}});
    apply_stack_state({0, 0, 100}, {200, 100, 100}, 100.0, load);
    REQUIRE(load.placements[0].supported_load == Catch::Approx(100.0));
    // 底座 max_load 100 已满载 → 再来一个 100kg 拒绝
    REQUIRE_FALSE(check_stack_constraints({0, 0, 100}, {200, 100, 100}, 100.0, load, btm));
}

TEST_CASE("max_load: 两支撑各 50%", "[core][stack]")
{
    auto bt = make_stack_bt("bt", std::nullopt, 40.0);
    std::map<std::string, BoxType> btm = {{"bt", bt}};
    auto load = make_stack_load();

    load.placements.push_back({"", "bt", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({0, 0, 0}, {100, 100, 100}, 0.0, load);
    load.placements.push_back({"", "bt", "", {100, 0, 0}, Orientation::XYZ, {100, 100, 100}});
    apply_stack_state({100, 0, 0}, {100, 100, 100}, 0.0, load);

    // 上箱 200x100 各占 50% → 各 50kg > max_load 40 → 拒绝
    REQUIRE_FALSE(check_stack_constraints({0, 0, 100}, {200, 100, 100}, 100.0, load, btm));

    // 面积加权拆分：50/50
    load.placements.push_back({"", "bt", "", {0, 0, 100}, Orientation::XYZ, {200, 100, 100}});
    apply_stack_state({0, 0, 100}, {200, 100, 100}, 100.0, load);
    REQUIRE(load.placements[0].supported_load == Catch::Approx(50.0));
    REQUIRE(load.placements[1].supported_load == Catch::Approx(50.0));
}

TEST_CASE("recompute_stack_state: 乱序重建", "[core][stack]")
{
    auto bt = make_stack_bt("bt", 3, 200.0);
    std::map<std::string, BoxType> btm = {{"bt", bt}};
    auto load = make_stack_load();

    // 乱序 push：顶层 b3、底层 b1、中层 b2，各重 50
    load.placements.push_back({"b3", "bt", "", {0, 0, 200}, Orientation::XYZ, {100, 100, 100}, "", "", 50.0});
    load.placements.push_back({"b1", "bt", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}, "", "", 50.0});
    load.placements.push_back({"b2", "bt", "", {0, 0, 100}, Orientation::XYZ, {100, 100, 100}, "", "", 50.0});

    std::vector<std::string> errs;
    recompute_stack_state(load, btm, &errs);
    REQUIRE(errs.empty());

    auto find_level = [&](const std::string& id)
    {
        for (const auto& pl : load.placements)
        {
            if (pl.box_id == id)
            {
                return std::make_pair(pl.stack_level, pl.supported_load);
            }
        }
        return std::make_pair(0, 0.0);
    };
    REQUIRE(find_level("b1") == std::make_pair(1, 50.0));
    REQUIRE(find_level("b2") == std::make_pair(2, 50.0));
    REQUIRE(find_level("b3") == std::make_pair(3, 0.0));
}

TEST_CASE("recompute_stack_state: 检测违例", "[core][stack]")
{
    // b1 max_stack=1（不可堆叠），b2 压在其上 → 违例
    auto b1 = make_stack_bt("b1", 1);
    auto b2 = make_stack_bt("b2", 5);
    std::map<std::string, BoxType> btm = {{"b1", b1}, {"b2", b2}};
    auto load = make_stack_load();

    load.placements.push_back({"b2", "b2", "", {0, 0, 100}, Orientation::XYZ, {100, 100, 100}});
    load.placements.push_back({"b1", "b1", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}});

    std::vector<std::string> errs;
    recompute_stack_state(load, btm, &errs);
    REQUIRE_FALSE(errs.empty());
    bool found = false;
    for (const auto& e : errs)
    {
        if (e.find("max_stack") != std::string::npos && e.find("b2") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

// 续装兜底路径：地板正中间放箱后，剩余空间必须完整覆盖（旧十字形切割会丢对角空间）
TEST_CASE("glc carve_out_space 完整覆盖剩余空间", "[core][glc]")
{
    using namespace pack3d::glc;

    Space root;
    root.pos = {0, 0, 0};
    root.lx = 100;
    root.ly = 100;
    root.lz = 50;
    root.id = 1;
    root.parent_id = -1;
    root.kind = SpaceKind::Root;

    // 地板正中间放 20×20×20 箱
    Placement pl;
    pl.position = {40, 40, 0};
    pl.osize = {20, 20, 20};

    std::vector<Space> stack;
    carve_out_space(root, pl, stack);

    // 体积守恒：互不相交的完整分解，总和 = space - box
    int64_t total = 0;
    for (const auto& s : stack)
    {
        total += static_cast<int64_t>(s.lx) * s.ly * s.lz;
    }
    REQUIRE(total == 100LL * 100 * 50 - 20LL * 20 * 20);

    // 四个对角地板区域（旧十字形切割全部丢失）必须可被某空间完整容纳
    auto can_hold = [](const Space& s, int32_t x, int32_t y, int32_t z,
                       int32_t dx, int32_t dy, int32_t dz)
    {
        return s.pos.x <= x && s.pos.y <= y && s.pos.z <= z &&
               s.pos.x + s.lx >= x + dx &&
               s.pos.y + s.ly >= y + dy &&
               s.pos.z + s.lz >= z + dz;
    };
    const std::array<Position, 4> diag = {Position{0, 0, 0}, Position{60, 0, 0},
                                          Position{0, 60, 0}, Position{60, 60, 0}};
    for (const auto& p : diag)
    {
        REQUIRE(std::any_of(stack.begin(), stack.end(), [&](const Space& s)
                            { return can_hold(s, p.x, p.y, p.z, 40, 40, 20); }));
    }
}
