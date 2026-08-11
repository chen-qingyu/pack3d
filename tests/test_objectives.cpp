#include "test_common.hpp"

// 目标字典序各维场景：min_container_count / min_platform_split / platform_merge /
// max_volume_rate / min_group_split 在固定目标顺序下的行为

// test_min_container.json — 2 个同型小箱，无平台/分组
// 固定目标: min_container_count 优先 → 1 个大容器
TEST_CASE("min_container_count", "[solver]")
{
    json input = load_data("data/tests/test_min_container.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
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
    json input = load_data("data/tests/test_min_platform.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        REQUIRE(res["summary"]["platform_split"] == 0);
        REQUIRE(res["summary"]["volume_rate"] < 1.0);
        REQUIRE(res["result"]["containers"].size() == 2);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "big");
        REQUIRE(res["result"]["containers"][0]["platforms"] == json::array({"A"}));
        REQUIRE(res["result"]["containers"][1]["type_id"] == "big");
        REQUIRE(res["result"]["containers"][1]["platforms"] == json::array({"B"}));
    }
}

// test_platform_merge.json — 前车[p1,p2]装满、尾车[p2,p3]未满
// 后处理必须在不新增容器的情况下把分散的 p2 并入尾车，使 platform_split = 0
// 合并 trial 先插入、失败则重排整个目标容器（目标自身箱子 + 捐献箱一起重新装载）。
// RGS 曾因 route 约束下排不出"深处平台先占 X 小侧"的布局而装不下 10 箱，
// 所有排序准则的确定性 pass 在 route 下统一按深度优先稳定排序（深处平台先放）后恢复通过。
TEST_CASE("platform_merge_no_new_container", "[solver]")
{
    json input = load_data("data/tests/test_platform_merge.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);

        // 失败诊断：算法 + 摘要 + 各容器平台分布（INFO 仅在断言失败时输出）
        INFO("algo=" << algo);
        INFO("summary=" << res["summary"].dump());
        std::string plat_desc;
        for (const auto& c : res["result"]["containers"])
        {
            plat_desc += c["type_id"].get<std::string>() + fmt::format(":[{}]", c["platforms"].get<std::vector<std::string>>());
        }
        INFO("containers=" << plat_desc);

        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["unpacked_box_count"] == 0);
        REQUIRE(res["summary"]["platform_split"] == 0);
        for (const auto& c : res["result"]["containers"])
        {
            REQUIRE(c["platforms"].size() <= 2);
        }
    }
}

// test_volume_first.json — 3 个同型小箱，无平台/分组
// 固定目标: min_container_count 优先 → 1 个大容器
TEST_CASE("max_volume_rate", "[solver]")
{
    json input = load_data("data/tests/test_volume_first.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
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
    json input = load_data("data/tests/test_group_split.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        REQUIRE(res["summary"]["group_split"] == 1);
        REQUIRE(res["summary"]["volume_rate"] == 1.0);
        REQUIRE(res["result"]["containers"].size() == 2);
        REQUIRE(res["result"]["containers"][0]["type_id"] == "big");
        REQUIRE(res["result"]["containers"][1]["type_id"] == "small");
    }
}
