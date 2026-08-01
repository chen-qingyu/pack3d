#include <catch2/catch_test_macros.hpp>

#include "core/io.hpp"
#include "core/types.hpp"

using namespace pack3d;

TEST_CASE("pre_validate_input 检测重复 ID", "[parser]")
{
    Problem p;
    p.time_limit = 30.0;
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
    p.time_limit = 30.0;
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
    p.time_limit = 30.0;
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
    p.time_limit = 30.0;
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
        if (v.find("max_load requires all boxes to have weight") != std::string::npos)
        {
            found = true;
        }
    }
    REQUIRE(found);
}
