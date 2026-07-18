#include <catch2/catch_test_macros.hpp>

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

    std::map<std::string, BoxType> btm;

    REQUIRE(check_support({0, 0, 0}, {100, 100, 100}, load, btm, 1.0));
    REQUIRE(check_support({0, 0, 0}, {100, 100, 100}, load, btm, 0.0));
}

TEST_CASE("check_support 部分支撑", "[core]")
{
    ContainerLoad load;
    load.type_id = "test";
    ContainerType ct{{}, {1000, 1000, 1000}, 10000.0};
    load.type = &ct;

    BoxType bt{"bt1", {100, 100, 100}, {Orientation::XYZ}};
    std::map<std::string, BoxType> btm = {{"bt1", bt}};

    load.placements.push_back({"", "bt1", "", {0, 0, 0}, Orientation::XYZ, {100, 100, 100}});
    load.used_volume = 100 * 100 * 100;
    load.total_weight = 100.0;

    REQUIRE(check_support({0, 0, 100}, {100, 100, 100}, load, btm, 0.0));
    REQUIRE(check_support({0, 0, 100}, {100, 100, 100}, load, btm, 1.0));
    REQUIRE(check_support({0, 0, 100}, {200, 100, 100}, load, btm, 0.5));
    REQUIRE_FALSE(check_support({0, 0, 100}, {200, 100, 100}, load, btm, 0.6));
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

    // 先放 A（深处）到 (0,0,0)
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

TEST_CASE("路线约束：YZ 重叠时先装平台必须在深处", "[core][route]")
{
    ContainerLoad load;
    load.type_id = "t";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    RouteOrder route{{"A", "B"}};
    route.index_of = {{"A", 0}, {"B", 1}};

    load.placements.push_back({"", "", "", {200, 0, 0}, Orientation::XYZ, {100, 100, 100}, "B"});

    // A 在 YZ 上与 B 重叠，且 X 在 B 右侧 → 拒绝
    REQUIRE_FALSE(check_route_order(load, "A", {250, 0, 0}, {100, 100, 100}, route));
    // A 完全在 B 左侧 → 允许
    REQUIRE(check_route_order(load, "A", {0, 0, 0}, {100, 100, 100}, route));
}

TEST_CASE("路线约束：XY 重叠时后装平台不能压住先装平台", "[core][route]")
{
    ContainerLoad load;
    load.type_id = "t";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    RouteOrder route{{"A", "B"}};
    route.index_of = {{"A", 0}, {"B", 1}};

    // A（先装）在 (0, 0, 100)，B（后装）在 (0, 0, 0)
    // XY 重叠，B.z=0 ≤ A.z=100 → B 没压住 A → 允许
    load.placements.push_back({"", "", "", {0, 0, 100}, Orientation::XYZ, {100, 100, 50}, "A"});
    REQUIRE(check_route_order(load, "B", {0, 0, 0}, {100, 100, 100}, route));
}

TEST_CASE("路线约束：后装平台压住先装平台被拒绝", "[core][route]")
{
    ContainerLoad load;
    load.type_id = "t";
    ContainerType ct{{}, {1000, 1000, 1000}};
    load.type = &ct;

    RouteOrder route{{"A", "B"}};
    route.index_of = {{"A", 0}, {"B", 1}};

    // A（先装）在 (0,0,0) 高 100，B（后装）在 (0,0,100) 直接叠在 A 上
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
