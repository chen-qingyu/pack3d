#include <fstream>
#include <sstream>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>

#include "core/app.hpp"
#include "core/io.hpp"
#include "core/types.hpp"

using namespace pack3d;

// 进程启动即全局关闭 spdlog，避免 Catch2 随机测试顺序导致日志时有时无
static const bool g_spdlog_off = (spdlog::set_level(spdlog::level::off), true);

// 从 JSON 文件加载测试场景
static json load_data(const char* path)
{
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

// run() 对任何非法输入都返回 status=invalid 的 JSON，而不是抛异常/崩溃
TEST_CASE("run 对畸形输入返回 invalid", "[solver]")
{
    // 合法基础结构（与 bsg_enforces_project_hard_constraints 相同的 initializer 风格）
    const auto make_base = []
    {
        json j;
        j["container_types"] = {{{"id", "t"}, {"inner_size", {{"x", 1}, {"y", 1}, {"z", 1}}}}};
        j["box_types"] = {{{"id", "b"},
                           {"size", {{"x", 1}, {"y", 1}, {"z", 1}}},
                           {"allowed_orientations", {"xyz"}}}};
        return j;
    };

    std::vector<json> bad_inputs;
    bad_inputs.push_back(json::object()); // 缺必需字段
    bad_inputs.push_back({{"container_types", json::array()},
                          {"box_types", json::array()},
                          {"boxes", json::array()}}); // minItems 违反

    auto dup = make_base();
    dup["boxes"] = {{{"id", "x"}, {"box_type_id", "b"}},
                    {{"id", "x"}, {"box_type_id", "b"}}}; // 重复 box id
    bad_inputs.push_back(dup);

    auto unknown = make_base();
    unknown["boxes"] = {{{"id", "x"}, {"box_type_id", "nope"}}}; // 未知箱型
    bad_inputs.push_back(unknown);

    auto no_weight_meta = make_base();
    no_weight_meta["boxes"] = {{{"id", "x"}, {"box_type_id", "b"}, {"weight", 5.0}}}; // 有重量但容器无 max_weight
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

// test_platform_merge.json — 前车[p1,p2]装满、尾车[p2,p3]未满
// 后处理必须在不新增容器的情况下把分散的 p2 并入尾车，使 platform_split = 0
TEST_CASE("platform_merge_no_new_container", "[solver]")
{
    auto base = load_data("data/tests/test_platform_merge.json");

    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        base["algorithm"] = algo;
        auto res = run(base);

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
                    // A 先卸 → 应在近门处（X 更大），即 A.x >= B.x + B.dx
                    CHECK(lhs_position["x"].get<int>() >= rhs_position["x"].get<int>() + rhs_size["dx"].get<int>());
                }
            }
        }
    }
}

// 3 个 100 立方体，限堆 2 层 + support_rate=1（禁止悬空）→ 第 3 个无法堆叠 → 需要第 2 个容器
TEST_CASE("max_stack 限制堆码层数", "[solver][stack]")
{
    json input = {
        {"container_types", {{{"id", "truck"}, {"inner_size", {{"x", 100}, {"y", 100}, {"z", 300}}}}}},
        {"box_types", {{{"id", "cube"}, {"size", {{"x", 100}, {"y", 100}, {"z", 100}}}, {"allowed_orientations", {"xyz"}}, {"max_stack", 2}}}},
        {"boxes", {
                      {{"id", "b1"}, {"box_type_id", "cube"}},
                      {{"id", "b2"}, {"box_type_id", "cube"}},
                      {{"id", "b3"}, {"box_type_id", "cube"}},
                  }},
        {"constraints", {{"support_rate", 1.0}}},
    };
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["unpacked_box_count"] == 0);
        REQUIRE(res["summary"]["container_count"] == 2);
        for (const auto& c : res["result"]["containers"])
        {
            REQUIRE(c["placements"].size() <= 2);
        }
    }
}

// max_load=40 < 单箱重量 50，support_rate=1（禁止悬空）：
// 每根柱最多 1 箱（任何叠放都会让支撑箱承重 50 > 40）→ 3 个箱子必须分到 3 个容器
TEST_CASE("max_load 限制单箱承重", "[solver][stack]")
{
    json input = {
        {"container_types", {{{"id", "truck"}, {"inner_size", {{"x", 100}, {"y", 100}, {"z", 300}}}, {"max_weight", 1000.0}}}},
        {"box_types", {{{"id", "cube"}, {"size", {{"x", 100}, {"y", 100}, {"z", 100}}}, {"allowed_orientations", {"xyz"}}, {"max_load", 40.0}}}},
        {"boxes", {
                      {{"id", "b1"}, {"box_type_id", "cube"}, {"weight", 50.0}},
                      {{"id", "b2"}, {"box_type_id", "cube"}, {"weight", 50.0}},
                      {{"id", "b3"}, {"box_type_id", "cube"}, {"weight", 50.0}},
                  }},
        {"constraints", {{"support_rate", 1.0}}},
    };
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["unpacked_box_count"] == 0);
        REQUIRE(res["summary"]["container_count"] == 3);
    }
}

// 非均匀重量下，max_load 用精确重量：容器 used_weight 必须等于各放置重量之和。
// 回归：GLC 之前按平均重量 + 队尾消耗，输出回填按队首，两者对非均匀重量会不一致。
TEST_CASE("max_load 非均匀重量保持重量一致性", "[solver][stack]")
{
    json input = {
        {"container_types", {{{"id", "truck"}, {"inner_size", {{"x", 300}, {"y", 100}, {"z", 220}}}, {"max_weight", 1000.0}}}},
        {"box_types", {
                          {{"id", "base"}, {"size", {{"x", 300}, {"y", 100}, {"z", 100}}}, {"allowed_orientations", {"xyz"}}, {"max_load", 70.0}},
                          {{"id", "top"}, {"size", {{"x", 150}, {"y", 100}, {"z", 100}}}, {"allowed_orientations", {"xyz"}}},
                      }},
        {"boxes", {
                      {{"id", "base1"}, {"box_type_id", "base"}, {"weight", 10.0}},
                      {{"id", "t1"}, {"box_type_id", "top"}, {"weight", 50.0}},
                      {{"id", "t2"}, {"box_type_id", "top"}, {"weight", 100.0}},
                      {{"id", "t3"}, {"box_type_id", "top"}, {"weight", 50.0}},
                  }},
        {"constraints", {{"support_rate", 1.0}}},
    };
    for (auto algo : {"gep", "glc", "rgs", "bsg"})
    {
        input["algorithm"] = algo;
        auto res = run(input);
        REQUIRE(res["status"] == "complete");
        for (const auto& c : res["result"]["containers"])
        {
            double sum = 0.0;
            for (const auto& pl : c["placements"])
            {
                sum += pl["weight"].get<double>();
            }
            REQUIRE(c["used_weight"].get<double>() == Catch::Approx(sum));
        }
    }
}

// 按朝向数组：仅 xzy（立放）能装入容器；max_stack=[2,1] 时立放限 1 层 → 需 2 容器
TEST_CASE("max_stack 按朝向数组", "[solver][stack]")
{
    auto make_input = [](int second_orient_limit) -> json
    {
        return {
            {"container_types", {{{"id", "truck"}, {"inner_size", {{"x", 200}, {"y", 50}, {"z", 220}}}}}},
            {"box_types", {{{"id", "box"}, {"size", {{"x", 200}, {"y", 100}, {"z", 50}}}, {"allowed_orientations", {"xyz", "xzy"}}, {"max_stack", {2, second_orient_limit}}}}},
            {"boxes", {
                          {{"id", "b1"}, {"box_type_id", "box"}},
                          {{"id", "b2"}, {"box_type_id", "box"}},
                      }},
        };
    };

    // 立放（xzy）限 1 层 → b2 无法叠 → 2 容器
    {
        auto input = make_input(1);
        input["algorithm"] = "gep";
        auto res = run(input);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["container_count"] == 2);
    }
    // 立放限 2 层 → 可叠 → 1 容器
    {
        auto input = make_input(2);
        input["algorithm"] = "gep";
        auto res = run(input);
        REQUIRE(res["status"] == "complete");
        REQUIRE(res["summary"]["container_count"] == 1);
    }
}
