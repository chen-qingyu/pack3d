#include <fstream>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <spdlog/spdlog.h>

#include "core/app.hpp"
#include "core/io.hpp"
#include "core/types.hpp"

using namespace hypercube;

// 从 JSON 文件加载测试场景
static json load_data(const char* path)
{
    spdlog::set_level(spdlog::level::off); // 关闭日志以免干扰测试输出
    std::ifstream ifs(path);
    REQUIRE(ifs.is_open());
    std::stringstream buf;
    buf << ifs.rdbuf();
    return json::parse(buf.str());
}

// 确保 4 个目标各自独立工作
TEST_CASE("independent_objectives", "[solver]")
{
    auto base = load_data("data/demo.json");

    const char* all_keys[] = {
        "min_container_count",
        "min_platform_count",
        "max_volume_rate",
        "min_group_split",
    };

    for (auto key : all_keys)
    {
        base["objectives"] = {key};
        auto res = run_solver(base.dump());
        REQUIRE(res["status"] == "success");
        REQUIRE(res["summary"]["objective_vector"].size() == 1);
        REQUIRE(res["summary"]["objective_vector"].contains(key));
        REQUIRE(res["result"]["containers"].size() >= 1);
    }
}

// test_min_container.json — 2 个同型小箱，无平台/分组
TEST_CASE("min_container_count", "[solver]")
{
    auto base = load_data("data/tests/test_min_container.json");

    SECTION("default -> 1 个大容器")
    {
        base["objectives"] = json::array();
        auto res = run_solver(base.dump());
        REQUIRE(res["summary"]["objective_vector"]["min_container_count"] == 1);
        REQUIRE(res["summary"]["objective_vector"]["max_volume_rate"] < 1.0);
        REQUIRE(res["result"]["containers"].size() == 1);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "big");
    }

    SECTION("max_volume_rate -> 2 个小容器，各 100%")
    {
        base["objectives"] = {"max_volume_rate", "min_container_count"};
        auto res = run_solver(base.dump());
        REQUIRE(res["summary"]["objective_vector"]["min_container_count"] == 2);
        REQUIRE(res["summary"]["objective_vector"]["max_volume_rate"] == 1.0);
        REQUIRE(res["result"]["containers"].size() == 2);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "small");
        REQUIRE(res["result"]["containers"][1]["type_id"] == "small");
    }
}

// test_min_platform.json — 大小箱子各 2，A/B 平台
TEST_CASE("min_platform_count", "[solver]")
{
    auto base = load_data("data/tests/test_min_platform.json");

    SECTION("default -> 两个大容器，各一个平台")
    {
        base["objectives"] = json::array();
        auto res = run_solver(base.dump());
        REQUIRE(res["summary"]["objective_vector"]["min_platform_count"] == 2);
        REQUIRE(res["summary"]["objective_vector"]["max_volume_rate"] < 1.0);
        REQUIRE(res["result"]["containers"].size() == 2);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "big");
        REQUIRE(res["result"]["containers"][0]["load_summary"]["platforms"] == json::array({"A"}));
        REQUIRE(res["result"]["containers"][1]["type_id"] == "big");
        REQUIRE(res["result"]["containers"][1]["load_summary"]["platforms"] == json::array({"B"}));
    }

    SECTION("max_volume_rate -> 一大一小，均 100%")
    {
        base["objectives"] = {"max_volume_rate", "min_platform_count"};
        auto res = run_solver(base.dump());
        REQUIRE(res["summary"]["objective_vector"]["min_platform_count"] == 3);
        REQUIRE(res["summary"]["objective_vector"]["max_volume_rate"] == 1.0);
        REQUIRE(res["result"]["containers"].size() == 2);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "big");
        REQUIRE(res["result"]["containers"][0]["load_summary"]["platforms"] == json::array({"A", "B"}));
        REQUIRE(res["result"]["containers"][1]["type_id"] == "small");
        REQUIRE(res["result"]["containers"][1]["load_summary"]["platforms"] == json::array({"B"}));
    }
}

// test_volume_first.json — 3 个同型小箱，无平台/分组
TEST_CASE("max_volume_rate", "[solver]")
{
    auto base = load_data("data/tests/test_volume_first.json");

    SECTION("default -> 1 个大容器")
    {
        base["objectives"] = json::array();
        auto res = run_solver(base.dump());
        REQUIRE(res["summary"]["objective_vector"]["max_volume_rate"] < 1.0);
        REQUIRE(res["summary"]["objective_vector"]["min_container_count"] == 1);
        REQUIRE(res["result"]["containers"].size() == 1);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "big");
    }

    SECTION("max_volume_rate -> 3 个小容器，各 100%")
    {
        base["objectives"] = {"max_volume_rate", "min_container_count"};
        auto res = run_solver(base.dump());
        REQUIRE(res["summary"]["objective_vector"]["max_volume_rate"] == 1.0);
        REQUIRE(res["summary"]["objective_vector"]["min_container_count"] == 3);
        REQUIRE(res["result"]["containers"].size() == 3);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "small");
        REQUIRE(res["result"]["containers"][1]["type_id"] == "small");
        REQUIRE(res["result"]["containers"][2]["type_id"] == "small");
    }
}

// test_group_split.json — 大小箱子各 2，A/B 分组
TEST_CASE("min_group_split", "[solver]")
{
    auto base = load_data("data/tests/test_group_split.json");

    SECTION("default -> 一大一小，组分散")
    {
        base["objectives"] = json::array();
        auto res = run_solver(base.dump());
        REQUIRE(res["summary"]["objective_vector"]["min_group_split"] == 3);
        REQUIRE(res["summary"]["objective_vector"]["max_volume_rate"] == 1.0);
        REQUIRE(res["result"]["containers"].size() == 2);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "big");
        REQUIRE(res["result"]["containers"][1]["type_id"] == "small");
    }

    SECTION("min_group_split -> 两个大容器，同组不分散")
    {
        base["objectives"] = {"min_group_split", "max_volume_rate"};
        auto res = run_solver(base.dump());
        REQUIRE(res["summary"]["objective_vector"]["min_group_split"] == 2);
        REQUIRE(res["summary"]["objective_vector"]["max_volume_rate"] < 1.0);
        REQUIRE(res["result"]["containers"].size() == 2);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "big");
        REQUIRE(res["result"]["containers"][1]["type_id"] == "big");
    }
}
