#include <catch2/catch_test_macros.hpp>

#include "core/io.hpp"
#include "core/types.hpp"

using namespace hypercube;

TEST_CASE("pre_validate_input 检测重复 ID", "[parser]")
{
    Problem p;
    p.time_limit_seconds = 30.0;
    p.container_types.push_back({"ct1", {1000, 1000, 1000}, 1000.0, std::nullopt});
    p.container_types.push_back({"ct1", {2000, 2000, 2000}, 2000.0, std::nullopt});
    p.box_types.push_back({"bt1", {100, 100, 100}, {Orientation::XYZ}});
    p.boxes.push_back({"box1", "bt1", 10.0, "", ""});

    auto violations = pre_validate_input(p);
    bool found_dup = false;
    for (const auto& v : violations)
    {
        if (v.details == reason::k_duplicate_id)
        {
            found_dup = true;
        }
    }
    REQUIRE(found_dup);
}

TEST_CASE("pre_validate_input 检测路线缺失平台", "[parser]")
{
    Problem p;
    p.time_limit_seconds = 30.0;
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
        if (v.details == reason::k_route_missing_platform)
        {
            found_route = true;
        }
    }
    REQUIRE(found_route);
}
