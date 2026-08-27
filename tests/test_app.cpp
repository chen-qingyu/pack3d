#include "test_common.hpp"

// run() 入口与输出契约：默认目标固定、输出字段恒存在、畸形输入不崩溃、
// 以及 BSG 场景下项目硬约束（重量/平台/路线）的遵守

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

    // 放置级
    for (const auto& c : res["result"]["containers"])
    {
        for (const auto& pl : c["placements"])
        {
            REQUIRE(pl.contains("platform"));
            REQUIRE(pl.contains("group"));
            REQUIRE(pl.contains("weight"));
            REQUIRE(pl.contains("is_pallet"));
        }
    }
}

TEST_CASE("箱型级 group/platform 传播到输出", "[solver][labels]")
{
    auto input = load_data("data/demo.json");
    input["box_types"][0]["group"] = "g1";
    input["box_types"][0]["platform"] = "P1";
    input["box_types"][1]["group"] = "g2";
    input["box_types"][1]["platform"] = "P2";
    input["route"] = {"P1", "P2"};

    auto res = run(input);
    REQUIRE(res["status"] == "complete");
    for (const auto& container : res["result"]["containers"])
    {
        for (const auto& placement : container["placements"])
        {
            // 箱型级 group/platform 应传播到每个 placement
            for (const auto& bt : input["box_types"])
            {
                if (bt["id"] == placement["box_type_id"])
                {
                    REQUIRE(placement["group"] == bt["group"]);
                    REQUIRE(placement["platform"] == bt["platform"]);
                    break;
                }
            }
        }
    }
}

TEST_CASE("空 group/platform 被 schema 拒绝", "[solver][labels]")
{
    auto input = load_data("data/demo.json");
    input["boxes"][0]["group"] = "";
    auto res = run(input);
    REQUIRE(res["status"] == "invalid");
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

// test_bsg_constraints.json — BSG 场景下项目硬约束：单容器重量 <= 10、平台不混装、路线先后
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
