#include <catch2/catch_test_macros.hpp>

#include "core/io.hpp"
#include "core/types.hpp"

using namespace pack3d;

TEST_CASE("pre_validate_input 检测重复 ID", "[parser]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    p.container_types.push_back({"ct1", {2000, 2000, 2000}, 2000.0, std::nullopt});
    p.box_types.push_back({"bt1", {100, 100, 100}, {Orientation::XYZ}});
    p.boxes.push_back({"box1", "bt1", 10.0, "", ""});

    auto violations = pre_validate_input(p);
    bool found_dup = false;
    for (const auto& v : violations)
    {
        if (v.find("duplicate") != std::string::npos)
        {
            found_dup = true;
        }
    }
    REQUIRE(found_dup);
}

TEST_CASE("pre_validate_input 检测路线缺失平台", "[parser]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    p.box_types.push_back({"bt1", {100, 100, 100}, {Orientation::XYZ}});
    p.boxes.push_back({"box1", "bt1", 10.0, "", "Z"});

    RouteOrder route;
    route.platform_order = {"A", "B"};
    route.index_of["A"] = 0;
    route.index_of["B"] = 1;
    p.route = route;

    auto violations = pre_validate_input(p);
    bool found_route = false;
    for (const auto& v : violations)
    {
        if (v.find("not in route") != std::string::npos)
        {
            found_route = true;
        }
    }
    REQUIRE(found_route);
}

TEST_CASE("pre_validate_input 检测 max_stack 数组长度不匹配", "[parser]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, std::nullopt, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ, Orientation::YXZ};
    bt.max_stack = {2, 3, 4}; // 长度 3 != 朝向数 2
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", std::nullopt, "", ""});

    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("max_stack array length") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("pre_validate_input 检测 max_load 需要重量", "[parser]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    bt.max_load = {50.0};
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", std::nullopt, "", ""}); // 无重量

    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("max_load requires weight info (box_types or boxes)") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

// 箱型级重量：箱型与箱子不能同时有重量
TEST_CASE("pre_validate_input 重量三选一：箱型与箱子重量互斥", "[parser]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    bt.weight = 10.0;
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", 5.0, "", ""}); // 箱子也带重量 → 互斥

    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("cannot coexist") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

// 箱型级重量：要么全部箱型有重量，要么全都没有
TEST_CASE("pre_validate_input 重量三选一：箱型重量必须全部配置", "[parser]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    BoxType bt1;
    bt1.id = "bt1";
    bt1.size = {100, 100, 100};
    bt1.allowed_orientations = {Orientation::XYZ};
    bt1.weight = 10.0;
    BoxType bt2;
    bt2.id = "bt2";
    bt2.size = {50, 50, 50};
    bt2.allowed_orientations = {Orientation::XYZ}; // 无重量
    p.box_types.push_back(bt1);
    p.box_types.push_back(bt2);
    p.boxes.push_back({"box1", "bt1", std::nullopt, "", ""});
    p.boxes.push_back({"box2", "bt2", std::nullopt, "", ""});

    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("some box_types have weight") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

// 箱型级重量模式（全箱型有、箱子无）合法
TEST_CASE("pre_validate_input 箱型级重量模式通过", "[parser]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    bt.weight = 10.0;
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", std::nullopt, "", ""});

    auto violations = pre_validate_input(p);
    REQUIRE(violations.empty());
}

// group 一致性：任一箱子有 group → 全部必须有（否则输出 tender 语义不一致）
TEST_CASE("pre_validate_input group 一致性：部分箱子缺 group 非法", "[parser]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", 5.0, "g1", ""});
    p.boxes.push_back({"box2", "bt1", 5.0, "", ""}); // 缺 group

    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("inconsistent group") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

// group 一致性：全有或全无均合法
TEST_CASE("pre_validate_input group 一致性：全有或全无均合法", "[parser]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", 5.0, "g1", ""});
    p.boxes.push_back({"box2", "bt1", 5.0, "g2", ""});
    REQUIRE(pre_validate_input(p).empty());

    Problem p2;
    p2.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    p2.box_types.push_back(bt);
    p2.boxes.push_back({"box1", "bt1", 5.0, "", ""});
    p2.boxes.push_back({"box2", "bt1", 5.0, "", ""});
    REQUIRE(pre_validate_input(p2).empty());
}

// group 一致性：已有放置缺 group 也判定非法
TEST_CASE("pre_validate_input group 一致性：已有放置缺 group 非法", "[parser]")
{
    Problem p;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    BoxType bt;
    bt.id = "bt1";
    bt.size = {100, 100, 100};
    bt.allowed_orientations = {Orientation::XYZ};
    p.box_types.push_back(bt);
    p.boxes.push_back({"box1", "bt1", 5.0, "g1", ""});
    ExistingPlacement ep;
    ep.box_id = "e1";
    ep.box_type_id = "bt1";
    ep.position = {0, 0, 0};
    ep.orientation = Orientation::XYZ;
    p.existing_containers.push_back({"ct1", {ep}}); // 已有放置缺 group

    auto violations = pre_validate_input(p);
    bool found = false;
    for (const auto& v : violations)
    {
        if (v.find("inconsistent group") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}
