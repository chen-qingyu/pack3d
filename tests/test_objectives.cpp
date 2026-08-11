#include "test_common.hpp"

// 确保 4 个目标固定为默认顺序，4 种算法均可完成
TEST_CASE("fixed_default_objectives", "[solver]")
{
    json input = load_data("data/demo.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["result"]["containers"].size() >= 1);
        // 输出不再包含 objective_keys 字段
        REQUIRE(!res["summary"].contains("objective_keys"));
    }
}

// 输出字段恒存在：未启用的功能给合理默认值（null / 空数组 / false）
TEST_CASE("输出字段恒存在", "[solver][output]")
{
    auto res = run(load_data("data/demo.json"));
    REQUIRE(res["status"] == "complete");

    // 顶层
    REQUIRE(res.contains("violations"));
    REQUIRE(res["violations"].is_array());
    REQUIRE(res["result"].contains("pallets"));
    REQUIRE(res["result"]["pallets"].is_array());
    REQUIRE(res["result"].contains("unpacked_boxes"));
    REQUIRE(res["result"]["unpacked_boxes"].is_array());

    // 容器级（demo 无障碍/斜面/站点/tender）
    for (const auto& c : res["result"]["containers"])
    {
        REQUIRE(c.contains("obstacles"));
        REQUIRE(c["obstacles"].is_array());
        REQUIRE(c.contains("facets"));
        REQUIRE(c["facets"].is_array());
        REQUIRE(c.contains("tender"));
        REQUIRE(c["tender"].is_null());
        REQUIRE(c.contains("payload"));
        REQUIRE(c.contains("used_weight"));
        REQUIRE(c.contains("weight_rate"));
        REQUIRE(c.contains("platforms"));
        REQUIRE(c.contains("groups"));
    }

    // 箱型级（demo 无承重/重量/散件）
    for (const auto& bt : res["result"]["box_types"])
    {
        REQUIRE(bt.contains("max_stack"));
        REQUIRE((bt["max_stack"].is_null() || bt["max_stack"].is_array()));
        REQUIRE(bt.contains("max_load"));
        REQUIRE((bt["max_load"].is_null() || bt["max_load"].is_array()));
        REQUIRE(bt.contains("weight"));
        REQUIRE(bt.contains("loose"));
    }

    // 放置级
    for (const auto& c : res["result"]["containers"])
    {
        for (const auto& pl : c["placements"])
        {
            REQUIRE(pl.contains("platform"));
            REQUIRE(pl.contains("group"));
            REQUIRE(pl.contains("weight"));
        }
    }
}

// run() 对任何非法输入都返回 status=invalid 的 JSON，而不是抛异常/崩溃
TEST_CASE("run 对畸形输入返回 invalid", "[solver]")
{
    // 合法基础结构（从 data/tests/test_invalid_base.json 加载）
    const auto base = load_data("data/tests/test_invalid_base.json");

    std::vector<json> bad_inputs;
    bad_inputs.push_back(json::object()); // 缺必需字段
    bad_inputs.push_back({{"container_types", json::array()},
                          {"box_types", json::array()},
                          {"boxes", json::array()}}); // minItems 违反

    auto dup = base;
    dup["boxes"] = {{{"id", "x"}, {"box_type_id", "b"}},
                    {{"id", "x"}, {"box_type_id", "b"}}}; // 重复 box id
    bad_inputs.push_back(dup);

    auto unknown = base;
    unknown["boxes"] = {{{"id", "x"}, {"box_type_id", "nope"}}}; // 未知箱型
    bad_inputs.push_back(unknown);

    auto no_weight_meta = base;
    no_weight_meta["boxes"] = {{{"id", "x"}, {"box_type_id", "b"}, {"weight", 5.0}}}; // 有重量但容器无 payload
    bad_inputs.push_back(no_weight_meta);

    for (const auto& input : bad_inputs)
    {
        const auto res = run(input);
        REQUIRE(res.contains("status"));
        REQUIRE(res["status"] == "invalid");
    }
}

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

TEST_CASE("bsg_enforces_project_hard_constraints", "[solver][bsg]")
{
    json input = load_data("data/tests/test_bsg_constraints.json");

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
                bool yz_overlap =
                    lhs["y"].get<int>() < rhs["y"].get<int>() + rhs["dy"].get<int>() &&
                    rhs["y"].get<int>() < lhs["y"].get<int>() + lhs["dy"].get<int>() &&
                    lhs["z"].get<int>() < rhs["z"].get<int>() + rhs["dz"].get<int>() &&
                    rhs["z"].get<int>() < lhs["z"].get<int>() + lhs["dz"].get<int>();
                if (yz_overlap && lhs["platform"] == "A" && rhs["platform"] == "B")
                {
                    // A 先卸 → 应在近门处（X 更大），即 A.x >= B.x + B.dx
                    CHECK(lhs["x"].get<int>() >= rhs["x"].get<int>() + rhs["dx"].get<int>());
                }
            }
        }
    }
}
