#include <fstream>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <spdlog/spdlog.h>

#include "core/app.hpp"
#include "core/io.hpp"
#include "core/types.hpp"

using namespace pack3d;

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

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
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

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
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

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
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

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
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

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
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

TEST_CASE("bsg_enforces_project_hard_constraints", "[solver][bsg]")
{
    json input = {
        {"algorithm", "bsg"},
        {"container_types", {{{"id", "truck"}, {"inner_size", {{"x", 100}, {"y", 100}, {"z", 50}}}, {"max_weight", 10.0}, {"quantity_limit", nullptr}}}},
        {"box_types", {{{"id", "half"}, {"size", {{"x", 50}, {"y", 100}, {"z", 50}}}, {"allowed_orientations", {"xyz"}}}}},
        {"boxes", {
                      {{"id", "later"}, {"box_type_id", "half"}, {"weight", 6.0}, {"platform", "B"}},
                      {{"id", "earlier"}, {"box_type_id", "half"}, {"weight", 6.0}, {"platform", "A"}},
                  }},
        {"route", {"A", "B"}},
        {"constraints", {{"time_limit", 5.0}, {"support_rate", 0.0}, {"platform_limit", 1}, {"tender_limit", nullptr}}},
    };

    auto result = run(input);
    REQUIRE(result["status"] == "complete");
    REQUIRE(result["summary"]["unpacked_box_count"] == 0);

    for (const auto& container : result["result"]["containers"])
    {
        CHECK(container["used_weight"].get<double>() <= 10.0);
        CHECK(container["platforms"].size() <= 1);

        const auto& placements = container["placements"];
        for (const auto& lhs : placements)
        {
            for (const auto& rhs : placements)
            {
                if (lhs["platform"] == rhs["platform"])
                {
                    continue;
                }
                const auto& lhs_position = lhs["position"];
                const auto& lhs_size = lhs["size"];
                const auto& rhs_position = rhs["position"];
                const auto& rhs_size = rhs["size"];
                bool yz_overlap =
                    lhs_position["y"].get<int>() < rhs_position["y"].get<int>() + rhs_size["dy"].get<int>() &&
                    rhs_position["y"].get<int>() < lhs_position["y"].get<int>() + lhs_size["dy"].get<int>() &&
                    lhs_position["z"].get<int>() < rhs_position["z"].get<int>() + rhs_size["dz"].get<int>() &&
                    rhs_position["z"].get<int>() < lhs_position["z"].get<int>() + lhs_size["dz"].get<int>();
                if (yz_overlap && lhs["platform"] == "A" && rhs["platform"] == "B")
                {
                    CHECK(lhs_position["x"].get<int>() + lhs_size["dx"].get<int>() <= rhs_position["x"].get<int>());
                }
            }
        }
    }
}
