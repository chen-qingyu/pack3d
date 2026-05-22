#include <catch2/catch_test_macros.hpp>

#include "core/constraints.hpp"
#include "core/geometry.hpp"
#include "core/objectives.hpp"
#include "core/types.hpp"

using namespace hypercube;

TEST_CASE("orient_size 产生正确的尺寸", "[core]")
{
    Size base{500, 400, 300};

    auto r1 = orient_size(base, Orientation::XYZ);
    REQUIRE(r1.dx == 500);
    REQUIRE(r1.dy == 400);
    REQUIRE(r1.dz == 300);

    auto r2 = orient_size(base, Orientation::YXZ);
    REQUIRE(r2.dx == 400);
    REQUIRE(r2.dy == 500);
    REQUIRE(r2.dz == 300);

    auto r3 = orient_size(base, Orientation::ZYX);
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
    REQUIRE(check_overlap({0, 0, 0}, {100, 100, 100},
                          {50, 50, 50}, {100, 100, 100}));
    REQUIRE_FALSE(check_overlap({0, 0, 0}, {100, 100, 100},
                                {200, 0, 0}, {100, 100, 100}));
    REQUIRE_FALSE(check_overlap({0, 0, 0}, {100, 100, 100},
                                {0, 200, 0}, {100, 100, 100}));
}

TEST_CASE("calc_support_ratio 在地板上为 1.0", "[core]")
{
    ContainerLoad load;
    load.type_id = "test";
    ContainerType ct{};
    ct.inner_size = {1000, 1000, 1000};
    load.type = &ct;

    std::map<std::string, BoxType> btm;
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    btm["bt1"] = bt;

    double ratio = calc_support_ratio({0, 0, 0}, {100, 100, 100}, load, btm);
    REQUIRE(ratio == 1.0);
}

TEST_CASE("calc_support_ratio 部分支撑", "[core]")
{
    ContainerLoad load;
    load.type_id = "test";
    ContainerType ct{};
    ct.inner_size = {1000, 1000, 1000};
    ct.max_weight = 10000.0;
    load.type = &ct;

    std::map<std::string, BoxType> btm;
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    btm["bt1"] = bt;

    Placement existing;
    existing.box_id = "existing";
    existing.box_type_id = "bt1";
    existing.position = {0, 0, 0};
    existing.orientation = Orientation::XYZ;
    load.placements.push_back(existing);
    load.used_volume = 100 * 100 * 100;
    load.total_weight = 100.0;

    double full = calc_support_ratio({0, 100, 0}, {100, 100, 100}, load, btm);
    REQUIRE(full == 1.0);

    double partial = calc_support_ratio({0, 100, 0}, {200, 100, 100}, load, btm);
    REQUIRE(partial == 0.5);
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

    auto r1 = check_platform_limit_constraint(load, "B", 1);
    REQUIRE_FALSE(r1.ok);

    auto r2 = check_platform_limit_constraint(load, "A", 1);
    REQUIRE(r2.ok);

    auto r3 = check_platform_limit_constraint(load, "B", 0);
    REQUIRE(r3.ok);
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
