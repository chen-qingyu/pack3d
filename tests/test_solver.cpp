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

// 确保 4 个目标固定为默认顺序，3 种算法均可完成
TEST_CASE("fixed_default_objectives", "[solver]")
{
    auto base = load_data("data/demo.json");

    for (auto algo : {"gep", "glc", "rgs"})
    {
        base["algorithm"] = algo;
        auto res = run(base);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["result"]["containers"].size() >= 1);
        // 输出不再包含 objective_keys 字段
        REQUIRE(!res["summary"].contains("objective_keys"));
    }
}

// test_min_container.json — 2 个同型小箱，无平台/分组
// 固定目标: min_container_count 优先 → 1 个大容器
TEST_CASE("min_container_count", "[solver]")
{
    auto base = load_data("data/tests/test_min_container.json");

    for (auto algo : {"gep", "glc", "rgs"})
    {
        base["algorithm"] = algo;
        auto res = run(base);
        REQUIRE(res["summary"]["container_count"] == 1);
        REQUIRE(res["summary"]["volume_rate"] < 1.0);
        REQUIRE(res["result"]["containers"].size() == 1);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "big");
    }
}

// test_min_platform.json — 大小箱子各 2，A/B 平台
// 固定目标: min_container_count > min_platform_split → 2 大容器，平台不分散
TEST_CASE("min_platform_split", "[solver]")
{
    auto base = load_data("data/tests/test_min_platform.json");

    for (auto algo : {"gep", "glc", "rgs"})
    {
        base["algorithm"] = algo;
        auto res = run(base);
        REQUIRE(res["summary"]["platform_split"] == 0);
        REQUIRE(res["summary"]["volume_rate"] < 1.0);
        REQUIRE(res["result"]["containers"].size() == 2);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "big");
        REQUIRE(res["result"]["containers"][0]["platforms"] == json::array({"A"}));
        REQUIRE(res["result"]["containers"][1]["type_id"] == "big");
        REQUIRE(res["result"]["containers"][1]["platforms"] == json::array({"B"}));
    }
}

// test_volume_first.json — 3 个同型小箱，无平台/分组
// 固定目标: min_container_count 优先 → 1 个大容器
TEST_CASE("max_volume_rate", "[solver]")
{
    auto base = load_data("data/tests/test_volume_first.json");

    for (auto algo : {"gep", "glc", "rgs"})
    {
        base["algorithm"] = algo;
        auto res = run(base);
        REQUIRE(res["summary"]["volume_rate"] < 1.0);
        REQUIRE(res["summary"]["container_count"] == 1);
        REQUIRE(res["result"]["containers"].size() == 1);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "big");
    }
}

// test_group_split.json — 大小箱子各 2，A/B 分组
// 固定目标: min_container_count > min_platform_split > max_volume_rate > min_group_split
TEST_CASE("min_group_split", "[solver]")
{
    auto base = load_data("data/tests/test_group_split.json");

    for (auto algo : {"gep", "glc", "rgs"})
    {
        base["algorithm"] = algo;
        auto res = run(base);
        REQUIRE(res["summary"]["group_split"] == 1);
        REQUIRE(res["summary"]["volume_rate"] == 1.0);
        REQUIRE(res["result"]["containers"].size() == 2);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "big");
        REQUIRE(res["result"]["containers"][1]["type_id"] == "small");
    }
}
